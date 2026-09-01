#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSystemTrayIcon>

#include "core/AppEntry.h"
#include "core/AppProvider.h"
#include "core/AutostartManager.h"
#include "core/CalculatorProvider.h"
#include "core/CurationStore.h"
#include "core/FavoritesStore.h"
#include "core/FileIndex.h"
#include "core/FileProvider.h"
#include "core/FileSearch.h"
#include "core/HotkeyManager.h"
#include "core/IconCache.h"
#include "core/IconProvider.h"
#include "core/LaunchController.h"
#include "core/LaunchHistory.h"
#include "core/LauncherController.h"
#include "core/ResultsModel.h"
#include "core/ScanService.h"
#include "core/SettingsStore.h"
#include "tray/TrayIcon.h"
#include "ui/HotkeyCaptureDialog.h"
#include "ui/SettingsWindow.h"
#include "win/WinDirectoryWalk.h"
#include "win/WinFullscreenGuard.h"
#include "win/WinIconExtractor.h"
#include "win/WinSingleInstance.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QProcess>
#include <QThreadPool>
#include <QTimer>
#include <memory>

#include "FrameTimeProbe.h"
#include "core/UpdateService.h"
#include "ui/UpdateDialogs.h"

int main(int argc, char *argv[])
{
    // ── SYS-01 (D-09): single-instance guard FIRST — before ANY window or
    // tray construction (CONTEXT boot order; UI-SPEC second-instance
    // contract: no UI surface, no toast). A duplicate process fails the
    // mutex acquire, signals the running instance to show the launcher via
    // the named-event channel, and exits silently.
    WinSingleInstance singleInstance;
    if (!singleInstance.tryAcquire()) {
        // Create-or-open + SetEvent: if the first instance's watcher is not
        // running yet, the auto-reset event stays signaled and fires the
        // moment it starts waiting (RESEARCH Pitfall 1 fix).
        singleInstance.signalShow();
        return 0; // second instance exits silently — no UI surface
    }

    // D-02.1: QSystemTrayIcon requires QApplication (Qt Widgets), not QGuiApplication.
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("TID");
    QCoreApplication::setApplicationName("wisp");
    // D-15 single version source: WISP_VERSION comes from CMake project(VERSION).
    QCoreApplication::setApplicationVersion(QStringLiteral(WISP_VERSION));

    // D-02.1 resident: the window hides, the app lives. SINGLE home of this
    // flag (the QML side must never set it).
    app.setQuitOnLastWindowClosed(false);

    QQuickWindow::setDefaultAlphaBuffer(true);

    // D-12 (SYS-02): the autostart Run key launches wisp with the documented
    // "--autostart" arg — consumed here with an EXACT-string match (T-06-07:
    // no other argv interpretation; unknown args ignored). The launcher is
    // resident-hidden at boot by design (D-02.1 — hotkey/tray summons it),
    // so the D-11 "skip the first show" quiet posture holds for both boot
    // paths; this parse is the D-12 consumption contract.
    const bool autostartBoot =
        QCoreApplication::arguments().contains(QStringLiteral("--autostart"));
    Q_UNUSED(autostartBoot)

    // Phase-3 vertical slice (03-05). Constructed BEFORE loadFromModule so the
    // context properties exist when MainWindow.qml type-compiles; all four are
    // window-independent (controller.setWindow happens after the window cast
    // below — state-updating wiring stays after load).
    ResultsModel resultsModel;      // context property "resultsModel"
    LaunchController launch;        // context property "launchController"
    LauncherController controller;  // hoisted here so the D-13 lambda can capture it
    LaunchHistory history;          // D-10/D-11 launch-tracking + added-exe store (04-03)
    FileSearch fileSearch;          // context property "fileSearch" — debounced worker (04-02)
    SettingsStore settingsStore;    // context property "settingsStore" — accent store (05-03, D-14)
    CurationStore curationStore;    // 05.1: UI-thread-only hide/show store (CUR-02)
    FavoritesStore favoritesStore;  // 2026-08-15: UI-thread-only favorites store (Favorites tab)
    AutostartManager autostart;     // 06-01: HKCU Run-key store (SYS-02, D-10/D-12)
    UpdateService updates(QStringLiteral(WISP_VERSION)); // Phase 8 auto-updater (installed-build gated)

    // ── Phase-7 (07-04): local index + scan service — the D-01 backend swap ──
    QThreadPool scanPool;                       // D-08: dedicated scan pool — never the global
    scanPool.setMaxThreadCount(1);              // single-flight already serializes; 1 thread suffices
    FileIndex index;                            // D-07: %APPDATA%\TID\wisp\wisp-index.dat
    ScanService scanService;                    // D-08/D-09 orchestration

    // D-09 startup load (A4 measurement): synchronous, BEFORE any seam wiring
    // and BEFORE the first dispatch — a corrupt/absent file degrades to an
    // empty index + first-scan path (Pitfall 8), never a crash. NO scan at
    // boot: ScanService.start() (below) only arms the interval timer.
    QElapsedTimer indexLoadTimer;
    indexLoadTimer.start();
    const bool indexLoaded = index.load();      // corrupt → false → empty index (Pitfall 8)
    qInfo("index load: %lldms (ok=%d)", indexLoadTimer.elapsed(), int(indexLoaded));

    // Phase-7 pivot (07-06): the user redirected the product: the launcher list is NEVER pre-populated — no Start
    // Menu / UWP enumeration feeds the model. The list stays empty until the
    // user picks folders to scan (07-06 launcher picker or Settings) and the
    // ScanService fills the index; query results are scan files only (plus
    // user-added executables). AppCatalog stays in the codebase for tests,
    // but main.cpp no longer constructs or starts it.
    launch.setModel(&resultsModel);                          // D-12 snapshot source
    // D-13: launch success dismisses INSTANTLY (hideNow — no animation wait).
    // The dismissal is controller-owned policy; QML never calls hideNow.
    launch.setDismissHandler([&controller] { controller.hideNow(); });

    // ── Phase-4 (04-05): file-search + launch-tracking seams ──
    launch.setHistory(&history); // D-10: Launched outcomes recorded (04-03)
    // D-01: index rows → AppEntry via the local prefilter (subsequence
    // superset — A3); the model re-ranks with FuzzyMatcher and caps at
    // kMaxFileRows=5. Runs on the FileSearch worker — never the UI thread.
    fileSearch.setQueryFn([&index](const QString &q) {
        return FileSearch::QueryResult{
            FileIndex::toEntries(index.queryCandidates(q), /*cap=*/1000), /*failed=*/false };
    });
    // D-16 on-query status — EXPLICIT ordinal map (never a blind cast) from
    // ScanService::ScanState to FileSearchState (07-03 → 07-04 contract):
    fileSearch.setStatusFn([&scanService] {
        switch (scanService.stateOrdinal()) {
        case int(ScanService::ScanState::NoRoots):   return int(FileSearch::FileSearchState::NoRoots);
        case int(ScanService::ScanState::Scanning):  return int(FileSearch::FileSearchState::Scanning);
        case int(ScanService::ScanState::Error):     return int(FileSearch::FileSearchState::Error);
        case int(ScanService::ScanState::Idle):      return int(FileSearch::FileSearchState::Idle);
        }
        return int(FileSearch::FileSearchState::Idle);
    });
    // 07-06: scan summary proxy — the launcher's empty state shows
    // "Last scan HH:mm — N entries" once a scan ran, so picking a folder
    // gives visible confirmation (the list itself fills on typing).
    fileSearch.setSummaryFn([&scanService] { return scanService.lastScanSummary(); });
    fileSearch.setTrackedSource([&history] { return history.trackedExecutables(); }); // D-06/D-10
    fileSearch.setAddExeDialog([] {                                                    // D-11 native dialog
        return QFileDialog::getOpenFileName(nullptr, QStringLiteral("Add executable"),
                                            QDir::homePath(), QStringLiteral("Executables (*.exe)"));
    });
    fileSearch.setAddEntryStore([&history](const QString &path) { history.addExecutable(path); });

    // ── Phase-7 (07-04): scan-service wiring (D-08/D-09) — after all
    // fileSearch seams so the settingsSource lambda sees a built store ──
    scanService.setListFn(&WinDirectoryWalk::winListDirectory);   // src/win firewall seam
    scanService.setIndex(&index);
    scanService.setPool(&scanPool);
    scanService.setSettingsSource([&settingsStore] {              // UI-thread snapshot (Pitfall 4)
        return ScanService::ScanSettings{ settingsStore.scanRoots(),
                                          settingsStore.scanIntervalMinutes() };
    });
    scanService.start();                                          // arms interval timer if roots exist (D-09)

    // 2026-08-15 (saved-list on open): the persisted index was loaded above
    // (index.load()) and start() never scans at boot (D-09), so dispatch the
    // CURRENT (empty) query NOW to render the saved list instantly instead of
    // leaving the launcher blank until the first interval scan or a keystroke.
    // refresh() → empty-query snapshot = whole loaded index + manual picks
    // (D-14); one worker round-trip, no scan, no debounce. Scans later update
    // it via scanStateChanged → refresh (below).
    fileSearch.refresh();

    // ── Phase-05.1 (05.1-04): curation seams — hide/show persistence from
    // the model. Hide must NEVER trigger ensureFresh/setEntries (research
    // Pitfall 3 — the model removes live). AppCatalog's curation source is
    // gone with the catalog (Phase-7 pivot (07-06)) — curation now applies to manual
    // picks only (fileSearch.setAddedSource below re-reads the store).
    resultsModel.setHideStore([&curationStore](const QString &id, bool hidden) {
        if (hidden) curationStore.hide(id); else curationStore.show(id);
    });
    // 2026-08-15: favorites seam — bind the store and seed the persisted set
    // at startup so the Favorites tab reflects prior sessions. Favorites are
    // id-based (targetPath/aumid), so file rows and manual picks favorite
    // like apps (no source exclusion — it's a positive marker, unlike curation).
    resultsModel.setFavoriteStore([&favoritesStore](const QString &id, bool favorite) {
        favoritesStore.setFavorite(id, favorite);
    });
    resultsModel.setFavoriteIds(favoritesStore.favoriteIds());
    // Frecency: history-backed boost (< tier gap, so tier order preserved)
    // Batched map = one lock + one allKeys scan per query (hot path) vs
    // per-result QSettings reads.
    resultsModel.setFrecencyMapFn([&history] {
        return history.allFrecencyBoosts();
    });
    // ── Phase-10 (provider fan-out): providers own typed-query search. Each
    // provider owns its index and runs in parallel via QtConcurrent (Results
    // Model fan-out — never the UI thread). FileSearch keeps the empty/default
    // list + status + refresh + add-executable.
    AppProvider appProvider;
    FileProvider fileProvider;
    CalculatorProvider calcProvider;
    fileProvider.setIndex(&index);
    fileProvider.setAddedSource([&history] { return history.addedExecutables(); });
    QVector<SearchProvider*> providers = { &appProvider, &fileProvider, &calcProvider };
    resultsModel.setProviders(providers);
    resultsModel.setPool(QThreadPool::globalInstance());
    // D-14: the default-list escape hatch — addedExecutables ONLY (never
    // launch history): manual picks join the empty-query list, launched
    // executables don't (CUR-04 intent). Stamped with the curation store so
    // a hidden manual pick stays hidden across dispatches (hideSelected
    // persists to the same store; this is the re-read side, AppCatalog
    // parity).
    fileSearch.setAddedSource([&history, &curationStore] {
        QVector<AppEntry> entries = history.addedExecutables();
        const QSet<QString> hidden = curationStore.hiddenIds();
        for (AppEntry &e : entries)
            e.hidden = hidden.contains(e.targetPath);
        return entries;
    });

    // ── Phase-5 (05-04): icon pipeline — provider registered before load ──
    // IconCache (bounded LRU, D-03) + IconProvider (image://wispicons/{id})
    // constructed BEFORE the engine so addImageProvider below can mount the
    // provider. Ownership is load-bearing: addImageProvider TRANSFERS
    // ownership to the engine — the engine delete's the provider at
    // destruction (fix 2026-08-10: it was stack-allocated here, so shutdown
    // freed a stack address → "_CrtIsValidHeapPointer" assertion at exit).
    // Stack order keeps the dependencies alive: the engine is declared after
    // cache, so destruction runs engine-first — the provider dies while the
    // cache it points into still exists.
    IconCache iconCache;                 // default cap 500 ≈ 8 MB (D-03)
    auto *iconProvider = new IconProvider(&iconCache, &WinIconExtractor::extract);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("wispicons"), iconProvider);
    engine.rootContext()->setContextProperty("resultsModel", &resultsModel);
    engine.rootContext()->setContextProperty("launchController", &launch);
    engine.rootContext()->setContextProperty("fileSearch", &fileSearch);
    // D-14: accent store — MainWindow's Component.onCompleted reads it once at
    // startup and Connections onAccentChanged feeds Theme.accent (05-05 t3).
    engine.rootContext()->setContextProperty("settingsStore", &settingsStore);
    engine.loadFromModule("wisp", "MainWindow");
    // D-?: the diagnostic trail context object (2026-08-11) — QML logs
    // key/ticker lifecycle events via launcherController.stateNote().
    engine.rootContext()->setContextProperty("launcherController", &controller);
    if (engine.rootObjects().isEmpty())
        return -1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    // Phase-7 pivot (07-06): no catalog → no setEntries wiring. The model starts and
    // stays empty; file results arrive via the FileSearch connect below.

    // D-15: generation-stamped file results → model merge (04-04). UI-thread
    // delivery guaranteed by FileSearch's watcher completion. WR-03: the query
    // text travels too — the model drops results computed for old text.
    QObject::connect(&fileSearch, &FileSearch::resultsReady, &resultsModel,
                     [&resultsModel](quint64 generation, const QString &query,
                                     const QVector<AppEntry> &files) {
                         resultsModel.setFileResults(generation, query, files);
                     });
    // 07-06: a scan state change (start / completion / failure) means the
    // INDEX changed without the query text changing — re-dispatch so the
    // default list fills with scanned executables the moment the walk lands
    // (previously the list only refreshed on typing, so picking a folder
    // appeared to do nothing). Cheap: one worker round-trip per transition.
    QObject::connect(&scanService, &ScanService::scanStateChanged, &fileSearch,
                     [&fileSearch] { fileSearch.refresh(); });

#ifdef QT_DEBUG
    // VISU-01 perf guard: log any frame over the 60fps budget (17ms).
    // Debug builds only — release carries zero overhead (compiled out).
    if (window)
        new FrameTimeProbe(window);   // intentionally unparented: lives for app.exec()
#endif

    HotkeyManager hotkeys;             // D-02.5: INI at %APPDATA%\TID\wisp\wisp.ini
    if (window)
        controller.setWindow(window);
    controller.setFullscreenGuard(&WinFullscreenGuard::currentState); // HOTK-04 hook

    // SYS-01 (D-09): a second launch summons THIS instance's launcher via the
    // named-event channel (showRequested is emitted from the watcher thread;
    // Qt auto-queues it to the GUI thread — WinHotkey precedent). Connected
    // BEFORE startWatching so no show request is ever missed: a signal that
    // lands pre-watch stays pending (Pitfall 1) and fires once the loop runs.
    // Explicit user intent → showUserRequested (bypasses the fullscreen guard,
    // same as the tray "Open wisp" path — a deliberate second launch).
    QObject::connect(&singleInstance, &WinSingleInstance::showRequested,
                     &controller, &LauncherController::showUserRequested);
    singleInstance.startWatching();

    TrayIcon tray(&app);           // constructed BEFORE hotkeys.start() so the
                                   // conflict notification is reachable at startup
    // UI-SPEC tray icon contract: the disc fill is live-bound to the accent.
    // Startup read (same silent-fallback path as the launcher — missing/corrupt
    // value → #0078D4) + accentChanged repaint below. TrayIcon never reaches
    // into the store itself (PATTERNS anti-pattern 1 — wiring lives HERE).
    tray.setAccent(settingsStore.accent());
    HotkeyCaptureDialog capture(&engine, &app);
    // SYS-03 (D-01/D-04): the settings surface controller — QML host for
    // SettingsWindow.qml + ColorDialog.qml via the shared engine; opened from
    // the tray AND the launcher footer-row gear (2026-08-15 user redesign —
    // D-04's tray-only rule is overridden; the gear seam lives on
    // LauncherController::setSettingsOpener). All collaborators injected
    // (06-03 contract); hotkey-capture handoff reopens the EXISTING dialog.
    SettingsWindow settingsWindow(&engine, &settingsStore, &autostart,
                                  &hotkeys, &capture, &scanService, &updates, &app);
    // 07-05: QFileDialog lives in QtWidgets (wisp_core does not link it) —
    // the native folder picker is injected here (FileSearch::setAddExeDialog
    // precedent, main.cpp:124-127) and SHARED by both consumers below.
    const auto pickFolder = [] {
        return QFileDialog::getExistingDirectory(
            nullptr, QStringLiteral("Add scan location"), QDir::homePath());
    };
    settingsWindow.setFolderPicker(pickFolder);
    // 07-06: the launcher's empty-state "Select a folder to scan" — same
    // picker, then persist the root (Pitfall-5 normalization inside
    // setScanRoots) and start scanning immediately (D-09 first-root flow).
    controller.setScanFolderAdder([&settingsStore, &scanService, pickFolder] {
        const QString dir = pickFolder();
        if (dir.isEmpty())
            return; // cancelled → nothing (D-11 cancel discipline)
        QStringList roots = settingsStore.scanRoots();
        if (roots.contains(QDir::toNativeSeparators(dir)))
            return; // duplicate root → no-op
        roots.append(dir);
        settingsStore.setScanRoots(roots);
        scanService.requestScan(); // D-09: scan starts the moment a root exists
    });

    // 2026-08-15: the footer-row settings gear — previously tray-only (D-04),
    // the user asked for an in-launcher affordance. Hide the launcher first so
    // the settings surface opens cleanly on top (same feel as click-away).
    controller.setSettingsOpener([&controller, &settingsWindow] {
        controller.hideNow();
        settingsWindow.open();
    });

    // ── Phase 8: update dialog host + flow wiring (CONTEXT D-01..D-14) ──
    // dlSource discriminates manual (prompt) from auto (toggle-ON) downloads:
    // failures toast on the silent auto path but render INLINE in Settings
    // for the manual path (D-10). 0=none 1=manual 2=auto.
    UpdateDialogs updateDialogs(&engine);
    auto dlSource = std::make_shared<int>(0);

    QObject::connect(&updates, &UpdateService::updateAvailable,
                     [&](const QString &v) {
                         tray.setUpdatePending(true, v);
                         // D-05/D-06 zero-interaction: toggle ON = download and
                         // install immediately whenever an update is found.
                         if (settingsStore.updatesAutoInstall()) {
                                                     *dlSource = 2; // silent: progress lives in Settings if open
                             updates.downloadAndInstall();
                         }
                     });
    QObject::connect(&updateDialogs, &UpdateDialogs::promptAccepted, [&] {
        if (updates.state() != UpdateService::State::Available)
            return;
        *dlSource = 1; // manual path - failures inline (Settings), success via updated toast
        updates.downloadAndInstall();
    });
    // promptRejected ("Later"/Esc): deliberate no-op — tomorrow's daily check
    // re-toasts while the old version persists (D-02).
    QObject::connect(&updates, &UpdateService::downloadProgress,
                     [&](qint64 received, qint64 total) {
                         // UX pass: the bar lives INSIDE Settings now; no
                         // floating window. Auto path stays fully quiet.
                         settingsWindow.setDownloadProgress(received, total);
                     });
    QObject::connect(&updates, &UpdateService::downloadVerified,
                     [&](const QString &installerPath) {
                         const QString v = updates.availableVersion();
                         { // one-shot "updated" marker consumed by the relaunched binary (D-14)
                             QSettings ini(QSettings::IniFormat, QSettings::UserScope,
                                           QStringLiteral("TID"), QStringLiteral("wisp"));
                             ini.setValue(QStringLiteral("updates/pendingInstalledVersion"), v);
                             ini.sync();
                         }
                         // T-08-07: argument-list form (no shell), path comes ONLY from
                         // the hash-verified signal. Installer /S relaunches wisp.exe.
                         QProcess::startDetached(installerPath, QStringList{QStringLiteral("/S")});
                         QCoreApplication::quit(); // exe cannot overwrite itself while running
                     });
    QObject::connect(&updates, &UpdateService::downloadFailed,
                     [&](const QString &reason) {
                         qWarning("UpdateService download failed: %s", qPrintable(reason));
                         // D-08 terminal: toast on the silent auto path only;
                         // the manual path's failure text lives in Settings (D-10).
                         if (*dlSource == 2)
                             tray.notifyUpdateFailed(updates.availableVersion());
                         *dlSource = 0;
                     });

    // ── Hotkey + tray wiring (2026-08-15 boot-race fix) ──
    // The autostart Run key can launch wisp BEFORE Explorer's notification
    // area exists (login): isSystemTrayAvailable() is false and the old code
    // fell into the tray-less branch forever — the app ran INVISIBLY with no
    // tray icon, which read as "autostart doesn't work". Now the tray wiring
    // RETRIES every second until the tray appears (cap 60s), then arms it.

    // Hotkey toggle + window deactivation: needed in BOTH branches — wire
    // unconditionally, before any tray logic.
    if (window) {
        QObject::connect(&hotkeys, &HotkeyManager::hotkeyPressed,
                         &controller, &LauncherController::toggle); // respects fullscreen guard
        QObject::connect(window, &QQuickWindow::activeChanged, &controller,
                         [window, &controller] {
                             controller.onWindowActiveChanged(window->isActive());
                         });
    }

    // Tray-less fallback: the window close becomes the reserved exit path.
    // Disconnected once the tray arms (the tray owns exit then).
    QMetaObject::Connection closeQuit;
    if (window) {
        closeQuit = QObject::connect(window, &QQuickWindow::closing, &app,
                                     [] { QCoreApplication::quit(); });
    }

    const auto armTray = [&]() -> bool {
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            return false;
        // ORDER IS LOAD-BEARING (HOTK-02 canonical scenario):
        //   1. tray.show() first — showMessage on a hidden tray is silently dropped
        //   2. connect(registrationFailed → notifyHotkeyConflict) BEFORE hotkeys.start()
        //   3. hotkeys.start() last
        tray.show();
        QObject::connect(&hotkeys, &HotkeyManager::registrationFailed,
                         &tray, &TrayIcon::notifyHotkeyConflict);
        QObject::connect(&tray, &TrayIcon::openWisp,
                         &controller, &LauncherController::showUserRequested);
        QObject::connect(&tray, &TrayIcon::changeHotkeyRequested, &capture,
                         [&capture, &hotkeys] {
                             hotkeys.suspend();
                             capture.open(hotkeys.hotkey().toString());
                         });
        QObject::connect(&capture, &HotkeyCaptureDialog::accepted, &hotkeys,
                         [&hotkeys](const QString &seq) {
                             hotkeys.setHotkey(QKeySequence(seq));
                             hotkeys.resume();
                         });
        QObject::connect(&capture, &HotkeyCaptureDialog::cancelled, &hotkeys,
                         [&hotkeys] { hotkeys.resume(); });
        QObject::connect(&tray, &TrayIcon::settingsRequested,
                         &settingsWindow, &SettingsWindow::open);
        QObject::connect(&settingsStore, &SettingsStore::accentChanged, &tray,
                         [&tray](const QColor &c) { tray.setAccent(c); });
        QObject::connect(&tray, &TrayIcon::quitRequested, &app, &QCoreApplication::quit);

        // ── Phase 8: update toasts need an armed tray; wire them here too ──
        QObject::connect(&tray, &TrayIcon::updateToastClicked, &updateDialogs, [&] {
            if (updates.state() == UpdateService::State::Available)
                updateDialogs.showPrompt(updates.availableVersion()); // D-01
        });
        QObject::connect(&tray, &TrayIcon::updateDownloadRequested, &updateDialogs, [&] {
            if (updates.state() == UpdateService::State::Available)
                updateDialogs.showPrompt(updates.availableVersion()); // D-03 menu item
        });

        hotkeys.start(); // idempotent — safe when the retry re-arms later
        if (window && closeQuit) {
            QObject::disconnect(closeQuit); // tray owns the exit now
            closeQuit = QMetaObject::Connection();
        }

        // ── Phase 8 startup sequence (once — guarded by the static flag) ──
        static bool updateChecksArmed = false;
        if (!updateChecksArmed) {
            updateChecksArmed = true;
            { // D-14 one-shot "updated" marker: written by the OLD binary before
              // it quit into the silent installer; consumed by THIS new binary.
                QSettings ini(QSettings::IniFormat, QSettings::UserScope,
                              QStringLiteral("TID"), QStringLiteral("wisp"));
                const QString pending =
                    ini.value(QStringLiteral("updates/pendingInstalledVersion")).toString();
                if (!pending.isEmpty()) {
                    ini.remove(QStringLiteral("updates/pendingInstalledVersion"));
                    ini.sync();
                    if (pending == QString::fromLatin1(WISP_VERSION))
                        tray.notifyUpdated(pending);
                }
            }
            // D-04: the daily check runs 30s after launch so its toast never
            // lands in the login burst. Guarded once-a-day inside the service;
            // toggle OFF only changes install behavior, not the check.
            QTimer::singleShot(30000, &updates, [&updates] { updates.checkForUpdates(false); });
        }
        return true;
    };

    if (armTray()) {
        // Tray available right now — done.
    } else {
        // Boot race: Explorer's tray not up yet (autostart at login).
        // Retry every 1s up to 60s; hotkeys still register meanwhile (the
        // fallback above kept the window-close exit).
        qWarning() << "System tray not available at boot — retrying...";
        hotkeys.start();
        QTimer trayRetry;
        trayRetry.setInterval(1000);
        QObject::connect(&trayRetry, &QTimer::timeout, &app, [&] {
            if (armTray()) {
                trayRetry.stop();
                qInfo() << "System tray armed after boot delay";
            }
        });
        trayRetry.start();
        // Note: retries are bounded by the app lifetime; isSystemTrayAvailable
        // polls cheaply and armTray is a no-op once armed (timer stopped).
    }

    // Phase-7 pivot (07-06): no catalog worker — nothing builds a default list at
    // boot. The ScanService interval timer (armed in scanService.start(),
    // line ~150) is the only background work: it rescans the user's roots.

    return app.exec();
}