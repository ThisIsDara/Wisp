#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSystemTrayIcon>

#include "core/AppCatalog.h"
#include "core/AutostartManager.h"
#include "core/CurationStore.h"
#include "core/FileIndex.h"
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
#include "win/WinStartMenuEnumerator.h"
#include "win/WinUwpEnumerator.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QThreadPool>

#include "FrameTimeProbe.h"

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
    QCoreApplication::setApplicationVersion("0.1.0");

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
    AppCatalog catalog;             // worker-built app inventory (D-08)
    LauncherController controller;  // hoisted here so the D-13 lambda can capture it
    LaunchHistory history;          // D-10/D-11 launch-tracking + added-exe store (04-03)
    FileSearch fileSearch;          // context property "fileSearch" — debounced worker (04-02)
    SettingsStore settingsStore;    // context property "settingsStore" — accent store (05-03, D-14)
    CurationStore curationStore;    // 05.1: UI-thread-only hide/show store (CUR-02)
    AutostartManager autostart;     // 06-01: HKCU Run-key store (SYS-02, D-10/D-12)

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

    catalog.setScanners({ &WinStartMenuEnumerator::scanStartMenu,
                          &WinUwpEnumerator::scanUwpApps }); // 03-02 real scanners
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
            FileIndex::toEntries(index.queryCandidates(q), /*cap=*/100), /*failed=*/false };
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

    // ── Phase-05.1 (05.1-04): curation seams — store reads at build time,
    // hide/show persistence from the model. Hide must NEVER trigger
    // ensureFresh/setEntries (research Pitfall 3 — the model removes live).
    catalog.setCurationSource([&curationStore] {
        return AppCatalog::CurationData{ curationStore.hiddenIds(), curationStore.shownIds() };
    });
    resultsModel.setHideStore([&curationStore](const QString &id, bool hidden) {
        if (hidden) curationStore.hide(id); else curationStore.show(id);
    });
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

    // D-08 silent swap: whenever the worker finishes a build, the model
    // receives the new snapshot (setEntries resets query → results clear
    // only when the catalog actually rebuilt; coherent, not broken).
    QObject::connect(&catalog, &AppCatalog::refreshed, &resultsModel,
                     [&resultsModel, &catalog] {
                         resultsModel.setEntries(catalog.entries());
                     });
    // D-08 age check on every show — the ONLY ensureFresh in this file
    // (wiring-check gate: never inside a HotkeyManager handler). The scan
    // itself runs on the catalog worker; this call is just the cheap check.
    if (window) {
        QObject::connect(window, &QQuickWindow::visibleChanged, &catalog,
                         [window, &catalog] {
                             if (window->isVisible())
                                 catalog.ensureFresh();
                         });
    }

    // D-15: generation-stamped file results → model merge (04-04). UI-thread
    // delivery guaranteed by FileSearch's watcher completion. WR-03: the query
    // text travels too — the model drops results computed for old text.
    QObject::connect(&fileSearch, &FileSearch::resultsReady, &resultsModel,
                     [&resultsModel](quint64 generation, const QString &query,
                                     const QVector<AppEntry> &files) {
                         resultsModel.setFileResults(generation, query, files);
                     });

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
    // the tray ONLY (no launcher affordance, D-04). All collaborators injected
    // (06-03 contract); hotkey-capture handoff reopens the EXISTING dialog.
    SettingsWindow settingsWindow(&engine, &settingsStore, &autostart,
                                  &hotkeys, &capture, &app);

    if (window && QSystemTrayIcon::isSystemTrayAvailable()) {
        // ORDER IS LOAD-BEARING (HOTK-02 canonical scenario):
        //   1. tray.show() first — showMessage on a hidden tray is silently dropped
        //   2. connect(registrationFailed → notifyHotkeyConflict) BEFORE hotkeys.start()
        //   3. hotkeys.start() last
        tray.show();

        QObject::connect(&hotkeys, &HotkeyManager::registrationFailed,
                         &tray, &TrayIcon::notifyHotkeyConflict);

        // Hotkey → toggle (respects the fullscreen guard via controller)
        QObject::connect(&hotkeys, &HotkeyManager::hotkeyPressed,
                         &controller, &LauncherController::toggle);
        // Window deactivation → controller grace timer (D-02.4 click-away)
        // QQuickWindow::activeChanged() is parameterless — adapter reads the
        // resulting active state.
        QObject::connect(window, &QQuickWindow::activeChanged, &controller,
                         [window, &controller] {
                             controller.onWindowActiveChanged(window->isActive());
                         });
        // Tray menu
        QObject::connect(&tray, &TrayIcon::openWisp,
                         &controller, &LauncherController::showUserRequested); // explicit intent bypasses guard
        QObject::connect(&tray, &TrayIcon::changeHotkeyRequested, &capture, [&capture, &hotkeys] {
            capture.open(hotkeys.hotkey().toString());
        });
        QObject::connect(&capture, &HotkeyCaptureDialog::accepted, &hotkeys,
                         [&hotkeys](const QString &seq) {
                             hotkeys.setHotkey(QKeySequence(seq)); // re-register + persist (D-02.5/6)
                         });
        // D-04: settings opens from the tray ONLY (no launcher affordance).
        QObject::connect(&tray, &TrayIcon::settingsRequested,
                         &settingsWindow, &SettingsWindow::open);
        // UI-SPEC tray contract: live disc repaint on accent change (D-06 —
        // persist-before-notify; the picker's accentChanged lands here).
        QObject::connect(&settingsStore, &SettingsStore::accentChanged, &tray,
                         [&tray](const QColor &c) { tray.setAccent(c); });
        QObject::connect(&tray, &TrayIcon::quitRequested, &app, &QCoreApplication::quit);

        hotkeys.start(); // register the persisted combo (registrationFailed may fire here → notified above)
    } else {
        // Tray-less fallback (exotic systems): D-02.1 "quit only via tray"
        // cannot hold without a tray — the window close becomes the reserved
        // explicit exit path. Hotkey toggle still works.
        if (window) {
            QObject::connect(&hotkeys, &HotkeyManager::hotkeyPressed,
                             &controller, &LauncherController::toggle);
            QObject::connect(window, &QQuickWindow::activeChanged, &controller,
                             [window, &controller] {
                                 controller.onWindowActiveChanged(window->isActive());
                             });
            QObject::connect(window, &QQuickWindow::closing, &app,
                             [] { QCoreApplication::quit(); }); // closing(QCloseEvent*) → no-arg quit
        }
        qWarning() << "No system tray available — running tray-less; window close is the exit.";
        hotkeys.start();
    }

    // D-08: kick the catalog worker build exactly ONCE — after all connects,
    // before the event loop. NEVER inside a hotkey handler (scanning on the
    // worker keeps the WM_HOTKEY path to a cheap age check, T-03-05-01).
    catalog.start();

    return app.exec();
}