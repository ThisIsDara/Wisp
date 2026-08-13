#include <QtTest>

#include <QTemporaryDir>

#include "core/AppEntry.h"
#include "core/LaunchController.h"
#include "core/LaunchHistory.h"
#include "core/ResultsModel.h"
#include "win/WinLaunch.h"

// LaunchController policy contract (D-11..D-13): D-12 snapshot freeze at
// keypress, D-11 UWP elevation refusal + quiet UAC-cancel, D-13 instant
// dismissal on success, launchIndex mouse path, null-model safety. The
// injectable Launcher/ResultReporter/DismissHandler seams (PATTERNS §2) prove
// every policy branch without any OS call — no real apps, no real UAC prompts.
// Phase-4 additions (04-03): D-05 silent-normal for elevated file/folder
// requests, LAUN-03 Ctrl+Enter reveal (file-only, D-12 freeze, D-13 dismiss),
// D-10 launch recording into a REAL LaunchHistory on a temp INI.

namespace {

AppEntry lnkEntry(const QString &name)
{
    AppEntry e;
    e.source = AppEntry::Source::Lnk;
    e.displayName = name;
    e.targetPath = QStringLiteral("C:\\apps\\%1.exe").arg(name.toCaseFolded());
    return e;
}

AppEntry uwpEntry(const QString &name)
{
    AppEntry e;
    e.source = AppEntry::Source::Uwp;
    e.displayName = name;
    e.aumid = QStringLiteral("SomeFamily!SomeAppId");
    return e;
}

// Source::File row (04-01): displayName = filename, targetPath = full path.
AppEntry fileEntry(const QString &name)
{
    AppEntry e;
    e.source = AppEntry::Source::File;
    e.displayName = name;
    e.targetPath = QStringLiteral("C:\\apps\\%1.exe").arg(name);
    return e;
}

// D-04 folder row: Source::File with isFolder (opens in Explorer).
AppEntry folderEntry(const QString &name)
{
    AppEntry e = fileEntry(name);
    e.isFolder = true;
    return e;
}

} // namespace

class TstLaunch : public QObject
{
    Q_OBJECT

private slots:
    void snapshotFreeze_D12();
    void uwpAdminRefused_D11();
    void classicElevatedLaunchesAndDismisses_D13();
    void quietUacCancel_D11();
    void launchFailureSignals();
    void launchIndexMousePath();
    void nullModelSafe();
    // ── Phase-4 additions (04-03) ──
    void fileRowEnterNormal_LAUN02();
    void fileRowElevatedSilentlyNormal_D05();
    void folderRowElevatedSilentlyNormal_D05();
    void revealCtrlEnter_LAUN03();
    void revealNoOpForApps();
    void revealFailureSignals();
    void launchTracking_D10();
    void uwpLaunchNotTracked();
    void revealSnapshotFreeze_D12();
};

void TstLaunch::snapshotFreeze_D12()
{
    ResultsModel model;
    model.setEntries({ lnkEntry(QStringLiteral("Alpha")),
                       lnkEntry(QStringLiteral("Beta")),
                       lnkEntry(QStringLiteral("Gamma")) });
    model.setQuery(QString());
    model.selectIndex(1);

    LaunchController c;
    c.setModel(&model);
    AppEntry launched;
    c.setLauncher([&](const AppEntry &e, bool, const LaunchController::ResultReporter &report) {
        launched = e;
        report(e, WinLaunch::LaunchResult::Launched);
    });

    // Enter on the current selection: the launcher receives the entry values
    // captured at call time (index 1 = Beta).
    c.launchSelected(false);
    QCOMPARE(launched.displayName, QStringLiteral("Beta"));
    QCOMPARE(launched.targetPath, QStringLiteral("C:\\apps\\beta.exe"));

    // Re-query reshuffles the list and resets the selection to 0 (D-02) —
    // the next launch is selection-aware (Gamma is now the only row).
    model.setQuery(QStringLiteral("gamma"));
    launched = {};
    c.launchSelected(false);
    QCOMPARE(launched.displayName, QStringLiteral("Gamma"));

    // Freeze core: the snapshot is taken BEFORE the launcher call — a
    // selection shift DURING the call cannot change the launched target.
    model.setQuery(QString());
    model.selectIndex(1); // Beta
    c.setLauncher([&](const AppEntry &e, bool, const LaunchController::ResultReporter &report) {
        launched = e;
        model.selectIndex(2); // shift mid-launch — must NOT affect the target
        report(e, WinLaunch::LaunchResult::Launched);
    });
    c.launchSelected(false);
    QCOMPARE(launched.displayName, QStringLiteral("Beta"));

    // The snapshot is a value copy — replacing the model's entries after the
    // call leaves the launched record untouched.
    model.setEntries({ uwpEntry(QStringLiteral("Replacement")) });
    QCOMPARE(launched.displayName, QStringLiteral("Beta"));
    QCOMPARE(launched.targetPath, QStringLiteral("C:\\apps\\beta.exe"));
}

void TstLaunch::uwpAdminRefused_D11()
{
    ResultsModel model;
    model.setEntries({ uwpEntry(QStringLiteral("Store App")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    int launches = 0;
    c.setLauncher([&](const AppEntry &, bool, const LaunchController::ResultReporter &) {
        ++launches;
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy refused(&c, &LaunchController::adminRequestRefused);
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    // Ctrl+Shift+Enter on a Store app: refuse BEFORE any launch attempt.
    c.launchSelected(true);
    QCOMPARE(refused.count(), 1);
    QCOMPARE(refused.takeFirst().at(0).toString(), QStringLiteral("Store App"));
    QCOMPARE(launches, 0);  // never an attempt (D-11)
    QCOMPARE(failed.count(), 0);
    QCOMPARE(dismisses, 0); // launcher stays open — the QML hint shows (03-05)
}

void TstLaunch::classicElevatedLaunchesAndDismisses_D13()
{
    ResultsModel model;
    model.setEntries({ lnkEntry(QStringLiteral("Terminal")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    AppEntry launched;
    bool elevated = false;
    c.setLauncher([&](const AppEntry &e, bool elev, const LaunchController::ResultReporter &report) {
        launched = e;
        elevated = elev;
        report(e, WinLaunch::LaunchResult::Launched);
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    // Ctrl+Shift+Enter on a classic app: elevated launch + instant dismissal
    // on success (D-13 hideNow path — the UAC prompt is async, hide now).
    c.launchSelected(true);
    QCOMPARE(launched.displayName, QStringLiteral("Terminal"));
    QVERIFY(elevated);
    QCOMPARE(dismisses, 1);
    QCOMPARE(failed.count(), 0);
}

void TstLaunch::quietUacCancel_D11()
{
    ResultsModel model;
    model.setEntries({ lnkEntry(QStringLiteral("Terminal")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    int launches = 0;
    AppEntry launched;
    c.setLauncher([&](const AppEntry &e, bool, const LaunchController::ResultReporter &report) {
        launched = e;
        ++launches;
        // Simulated outcome of the real WinLaunch::launchClassic: the user
        // cancelled the UAC prompt (ERROR_CANCELLED / SE_ERR_ACCESSDENIED).
        report(e, WinLaunch::LaunchResult::CancelledByUser);
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy refused(&c, &LaunchController::adminRequestRefused);
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    c.launchSelected(false);
    QCOMPARE(launches, 1); // the launch WAS attempted once
    QCOMPARE(launched.displayName, QStringLiteral("Terminal"));
    // D-11: cancelled UAC is a quiet no-op — zero signals, no dismissal;
    // the launcher stays open for the next attempt.
    QCOMPARE(refused.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(dismisses, 0);
}

void TstLaunch::launchFailureSignals()
{
    ResultsModel model;
    model.setEntries({ lnkEntry(QStringLiteral("Notepad")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    c.setLauncher([&](const AppEntry &e, bool, const LaunchController::ResultReporter &report) {
        report(e, WinLaunch::LaunchResult::Failed);
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    c.launchSelected(false);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.takeFirst().at(0).toString(), QStringLiteral("Notepad"));
    QCOMPARE(dismisses, 0); // no dismissal — the user can pick another row
}

void TstLaunch::launchIndexMousePath()
{
    ResultsModel model;
    model.setEntries({ lnkEntry(QStringLiteral("Alpha")),
                       lnkEntry(QStringLiteral("Beta")),
                       lnkEntry(QStringLiteral("Gamma")) });
    model.setQuery(QString());
    model.selectIndex(0);

    LaunchController c;
    c.setModel(&model);
    AppEntry launched;
    bool elevated = false;
    c.setLauncher([&](const AppEntry &e, bool elev, const LaunchController::ResultReporter &report) {
        launched = e;
        elevated = elev;
        report(e, WinLaunch::LaunchResult::Launched);
    });

    // Mouse click on row 2 launches row 2 regardless of the current selection.
    c.launchIndex(2, false);
    QCOMPARE(launched.displayName, QStringLiteral("Gamma"));
    QVERIFY(!elevated);

    // The elevated flag is forwarded on the click path too.
    c.launchIndex(0, true);
    QCOMPARE(launched.displayName, QStringLiteral("Alpha"));
    QVERIFY(elevated);
}

void TstLaunch::nullModelSafe()
{
    LaunchController c; // no model set (03-05 wires it later)
    int launches = 0;
    c.setLauncher([&](const AppEntry &, bool, const LaunchController::ResultReporter &) {
        ++launches;
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy refused(&c, &LaunchController::adminRequestRefused);
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    // Both call paths are safe no-ops: no crash, no signals, no launches.
    c.launchSelected(false);
    c.launchIndex(1, true);
    QCOMPARE(launches, 0);
    QCOMPARE(refused.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(dismisses, 0);
}

void TstLaunch::fileRowEnterNormal_LAUN02()
{
    ResultsModel model;
    model.setEntries({ fileEntry(QStringLiteral("Alpha")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    AppEntry launched;
    bool elevated = true;
    c.setLauncher([&](const AppEntry &e, bool elev, const LaunchController::ResultReporter &) {
        launched = e;
        elevated = elev;
    });
    QSignalSpy refused(&c, &LaunchController::adminRequestRefused);
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    // Enter on a file row: plain launch, no elevation — the entry reaches
    // the launcher untouched (LAUN-02 open-with-default-app path).
    c.launchSelected(false);
    QCOMPARE(launched.displayName, QStringLiteral("Alpha"));
    QCOMPARE(launched.targetPath, QStringLiteral("C:\\apps\\Alpha.exe"));
    QVERIFY(!elevated);
    QCOMPARE(refused.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void TstLaunch::fileRowElevatedSilentlyNormal_D05()
{
    ResultsModel model;
    model.setEntries({ fileEntry(QStringLiteral("Alpha")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    bool elevated = true;
    int launches = 0;
    c.setLauncher([&](const AppEntry &, bool elev, const LaunchController::ResultReporter &) {
        elevated = elev;
        ++launches;
        // no report — the outcome is decided by the launcher, not asserted
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy refused(&c, &LaunchController::adminRequestRefused);
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    // Ctrl+Shift+Enter on a FILE row: D-05 silent-normal — elevation applies
    // only to classic apps; no hint, no refusal UI, no dismissal.
    c.launchSelected(true);
    QCOMPARE(launches, 1);
    QVERIFY(!elevated);
    QCOMPARE(refused.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(dismisses, 0);
}

void TstLaunch::folderRowElevatedSilentlyNormal_D05()
{
    ResultsModel model;
    model.setEntries({ folderEntry(QStringLiteral("Games")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    bool elevated = true;
    int launches = 0;
    c.setLauncher([&](const AppEntry &e, bool elev, const LaunchController::ResultReporter &) {
        elevated = elev;
        ++launches;
        QVERIFY(e.isFolder);
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy refused(&c, &LaunchController::adminRequestRefused);
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    // Ctrl+Shift+Enter on a FOLDER row: same D-05 mapping (folders are
    // Source::File rows) — silently normal, zero signals.
    c.launchSelected(true);
    QCOMPARE(launches, 1);
    QVERIFY(!elevated);
    QCOMPARE(refused.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(dismisses, 0);
}

void TstLaunch::revealCtrlEnter_LAUN03()
{
    ResultsModel model;
    model.setEntries({ fileEntry(QStringLiteral("Alpha")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    QString revealedPath;
    c.setRevealer([&](const QString &path) {
        revealedPath = path;
        return WinLaunch::LaunchResult::Launched;
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });

    // Ctrl+Enter on a file row: the revealer receives the FULL path
    // (LAUN-03 Explorer select) and success dismisses (D-13).
    c.revealSelected();
    QCOMPARE(revealedPath, QStringLiteral("C:\\apps\\Alpha.exe"));
    QCOMPARE(dismisses, 1);
}

void TstLaunch::revealNoOpForApps()
{
    ResultsModel model;
    model.setEntries({ lnkEntry(QStringLiteral("Alpha")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    int reveals = 0;
    c.setRevealer([&](const QString &) {
        ++reveals;
        return WinLaunch::LaunchResult::Launched;
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy refused(&c, &LaunchController::adminRequestRefused);
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    // LAUN-03 is file-only: Ctrl+Enter on an app row is a quiet no-op —
    // the revealer is never called (T-04-09: no explorer.exe for non-files).
    c.revealSelected();
    QCOMPARE(reveals, 0);
    QCOMPARE(refused.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(dismisses, 0);
}

void TstLaunch::revealFailureSignals()
{
    ResultsModel model;
    model.setEntries({ fileEntry(QStringLiteral("Alpha")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    c.setRevealer([](const QString &) { return WinLaunch::LaunchResult::Failed; });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });
    QSignalSpy failed(&c, &LaunchController::launchFailed);

    // Reveal failure surfaces launchFailed (never a crash), no dismissal —
    // the user can pick another row.
    c.revealSelected();
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.takeFirst().at(0).toString(), QStringLiteral("Alpha"));
    QCOMPARE(dismisses, 0);
}

void TstLaunch::launchTracking_D10()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));
    LaunchHistory history(iniPath);

    ResultsModel model;
    model.setEntries({ fileEntry(QStringLiteral("Alpha")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    c.setHistory(&history);
    // Recording launcher reports Launched → the DEFAULT ResultReporter runs:
    // D-10 recordLaunch into the real store + D-13 dismiss.
    c.setLauncher([&](const AppEntry &e, bool, const LaunchController::ResultReporter &report) {
        report(e, WinLaunch::LaunchResult::Launched);
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });

    c.launchSelected(false);
    QCOMPARE(history.launchCount(QStringLiteral("C:\\apps\\Alpha.exe")), 1);
    const QVector<AppEntry> tracked = history.trackedExecutables();
    QCOMPARE(tracked.size(), 1);
    QCOMPARE(tracked.at(0).targetPath, QStringLiteral("C:\\apps\\Alpha.exe"));
    QCOMPARE(dismisses, 1); // default reporter still dismisses on Launched (D-13)
}

void TstLaunch::uwpLaunchNotTracked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));
    LaunchHistory history(iniPath);

    ResultsModel model;
    model.setEntries({ uwpEntry(QStringLiteral("Store App")) });
    model.setQuery(QString());

    LaunchController c;
    c.setModel(&model);
    c.setHistory(&history);
    c.setLauncher([&](const AppEntry &e, bool, const LaunchController::ResultReporter &report) {
        report(e, WinLaunch::LaunchResult::Launched);
    });
    c.setDismissHandler([] {});

    // A launched UWP row has an empty targetPath → never recorded (D-10).
    c.launchSelected(false);
    QCOMPARE(history.trackedExecutables().size(), 0);
    QCOMPARE(history.launchCount(QStringLiteral("C:\\apps\\Store App.exe")), 0);
}

void TstLaunch::revealSnapshotFreeze_D12()
{
    ResultsModel model;
    model.setEntries({ fileEntry(QStringLiteral("Alpha")),
                       fileEntry(QStringLiteral("Beta")) });
    model.setQuery(QString());
    model.selectIndex(1);

    LaunchController c;
    c.setModel(&model);
    QString revealedPath;
    c.setRevealer([&](const QString &path) {
        revealedPath = path;
        model.selectIndex(0); // selection shift mid-call — must not change the target
        return WinLaunch::LaunchResult::Launched;
    });
    int dismisses = 0;
    c.setDismissHandler([&] { ++dismisses; });

    // D-12: the path is frozen at keypress — the revealer receives the
    // PRE-shift selection (index 1 = Beta).
    c.revealSelected();
    QCOMPARE(revealedPath, QStringLiteral("C:\\apps\\Beta.exe"));
    QCOMPARE(dismisses, 1);

    // The snapshot is a value copy — entries replaced after the call leave
    // the recorded path untouched.
    model.setEntries({ fileEntry(QStringLiteral("Gamma")) });
    QCOMPARE(revealedPath, QStringLiteral("C:\\apps\\Beta.exe"));
}

QTEST_MAIN(TstLaunch)
#include "tst_launch.moc"