#include <QtTest>

#include <QtConcurrent>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>

#include "core/AppEntry.h"
#include "core/LaunchHistory.h"

// LaunchHistory store contract (D-10/D-11): every executable wisp launches is
// recorded {path → count} into the existing wisp INI (QSettings IniFormat,
// HotkeyManager pattern — PATTERNS §5), and manually added executables live
// in their own group that is never pruned. All keys are native-separator
// normalized so '/' can never be misread as a QSettings group separator.
// Every suite round-trips through a REAL temp INI (QTemporaryDir) — nothing
// touches %APPDATA% (default-path ctor) in CI.

namespace {

AppEntry fileEntry(const QString &displayName, const QString &targetPath)
{
    AppEntry e;
    e.source = AppEntry::Source::File;
    e.displayName = displayName;
    e.targetPath = targetPath;
    return e;
}

} // namespace

class TstHistory : public QObject
{
    Q_OBJECT

private slots:
    void recordAndReloadRoundTrip_D10();
    void sourceFileTag();
    void manualAddPersists_D11();
    void addedOnlyIsolation_D14();
    void unionDedupe();
    void forwardSlashNormalization();
    void uwpSkipped();
    void countsIndependentOfName();
    void concurrentAccessThreadSafe_WR01();
};

void TstHistory::recordAndReloadRoundTrip_D10()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    LaunchHistory history(iniPath);
    history.recordLaunch(fileEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\Alpha.exe")));
    history.recordLaunch(fileEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\Alpha.exe")));
    history.recordLaunch(fileEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\Beta.exe")));

    // NEW instance on the SAME ini path → the counts survived the reload
    // (persistence round-trip, not just in-memory state).
    LaunchHistory reloaded(iniPath);
    QCOMPARE(reloaded.launchCount(QStringLiteral("C:\\apps\\Alpha.exe")), 2);
    QCOMPARE(reloaded.launchCount(QStringLiteral("C:\\apps\\Beta.exe")), 1);
    QCOMPARE(reloaded.launchCount(QStringLiteral("C:\\apps\\Unknown.exe")), 0);
    const QVector<AppEntry> tracked = reloaded.trackedExecutables();
    QCOMPARE(tracked.size(), 2);
}

void TstHistory::sourceFileTag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    LaunchHistory history(iniPath);
    history.recordLaunch(fileEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\Alpha.exe")));

    const QVector<AppEntry> tracked = history.trackedExecutables();
    QCOMPARE(tracked.size(), 1);
    QCOMPARE(tracked.at(0).source, AppEntry::Source::File);
    // displayName is DERIVED from the path (filename) — never stored (D-10
    // single source of truth; a renamed file shows its new name).
    QCOMPARE(tracked.at(0).displayName, QStringLiteral("Alpha.exe"));
    QCOMPARE(tracked.at(0).targetPath, QStringLiteral("C:\\apps\\Alpha.exe"));
}

void TstHistory::manualAddPersists_D11()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    LaunchHistory history(iniPath);
    history.addExecutable(QStringLiteral("C:\\apps\\WoW.exe"));

    LaunchHistory reloaded(iniPath);
    const QVector<AppEntry> tracked = reloaded.trackedExecutables();
    QCOMPARE(tracked.size(), 1);
    QCOMPARE(tracked.at(0).targetPath, QStringLiteral("C:\\apps\\WoW.exe"));
    QCOMPARE(tracked.at(0).displayName, QStringLiteral("WoW.exe"));
}

void TstHistory::addedOnlyIsolation_D14()
{
    // D-14 (default list): addedExecutables() is the CUR-04 escape-hatch
    // channel — it returns MANUAL PICKS ONLY. Launch-tracked exes (D-10)
    // never leak into the default list, even when they also match.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    LaunchHistory history(iniPath);
    history.recordLaunch(fileEntry(QStringLiteral("Run"), QStringLiteral("C:\\apps\\Run.exe")));
    history.addExecutable(QStringLiteral("C:\\apps\\Picked.exe"));

    const QVector<AppEntry> added = history.addedExecutables();
    QCOMPARE(added.size(), 1);
    QCOMPARE(added.at(0).targetPath, QStringLiteral("C:\\apps\\Picked.exe"));
    QCOMPARE(added.at(0).displayName, QStringLiteral("Picked.exe"));
    // The launch-tracked exe stays in the tracked union (live-query source)…
    QCOMPARE(history.trackedExecutables().size(), 2);
    // …but never in the added channel. (Path identity — AppEntry has no
    // operator==; the added pick survives in the union: present in tracked.)
    QStringList trackedPaths;
    for (const AppEntry &e : history.trackedExecutables())
        trackedPaths << e.targetPath;
    for (const AppEntry &e : added)
        QVERIFY(trackedPaths.contains(e.targetPath));
}

void TstHistory::unionDedupe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    LaunchHistory history(iniPath);
    history.recordLaunch(fileEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\Alpha.exe")));
    history.addExecutable(QStringLiteral("C:\\apps\\Alpha.exe"));

    // Same path in both stores → ONE entry (union-deduped by path).
    const QVector<AppEntry> tracked = history.trackedExecutables();
    QCOMPARE(tracked.size(), 1);
    QCOMPARE(tracked.at(0).targetPath, QStringLiteral("C:\\apps\\Alpha.exe"));
}

void TstHistory::forwardSlashNormalization()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // QFileDialog returns '/'-separated paths; the key must be native —
    // '/' is the QSettings group separator and would corrupt the key.
    LaunchHistory history(iniPath);
    history.addExecutable(QStringLiteral("C:/Games/WoW.exe"));

    const QVector<AppEntry> tracked = history.trackedExecutables();
    QCOMPARE(tracked.size(), 1);
    QCOMPARE(tracked.at(0).targetPath, QStringLiteral("C:\\Games\\WoW.exe"));
    QCOMPARE(history.launchCount(QStringLiteral("C:/Games/WoW.exe")), 0); // no accidental history entry
}

void TstHistory::uwpSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    AppEntry uwp;
    uwp.source = AppEntry::Source::Uwp;
    uwp.displayName = QStringLiteral("Store App");
    uwp.aumid = QStringLiteral("SomeFamily!SomeAppId");
    // targetPath intentionally empty — UWP rows are never tracked (D-10).

    LaunchHistory history(iniPath);
    history.recordLaunch(uwp);

    QCOMPARE(history.launchCount(QStringLiteral("C:\\apps\\Alpha.exe")), 0);
    QCOMPARE(history.trackedExecutables().size(), 0);
}

void TstHistory::countsIndependentOfName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    LaunchHistory history(iniPath);
    history.recordLaunch(fileEntry(QStringLiteral("Old Name"), QStringLiteral("C:\\apps\\Beta.exe")));
    history.recordLaunch(fileEntry(QStringLiteral("New Name"), QStringLiteral("C:\\apps\\Beta.exe")));

    QCOMPARE(history.launchCount(QStringLiteral("C:\\apps\\Beta.exe")), 2);
    const QVector<AppEntry> tracked = history.trackedExecutables();
    QCOMPARE(tracked.size(), 1);
    // The stored entry never carries a displayName — the current filename
    // (path-derived) is what surfaces.
    QCOMPARE(tracked.at(0).displayName, QStringLiteral("Beta.exe"));
}

void TstHistory::concurrentAccessThreadSafe_WR01()
{
    // WR-01 regression: ONE QSettings instance is touched from two threads in
    // production — recordLaunch/addExecutable on the UI thread (LaunchController
    // reporter) while the file-search worker calls trackedExecutables. The
    // mutex guard must serialize them; without it a torn read loses increments.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    LaunchHistory history(iniPath);
    const QString path = QStringLiteral("C:\\apps\\Race.exe");
    const AppEntry entry = fileEntry(QStringLiteral("Race"), path);

    std::atomic<bool> start{ false };
    const auto writer = [&] {
        while (!start.load())
            QThread::yieldCurrentThread();
        for (int i = 0; i < 250; ++i)
            history.recordLaunch(entry);
    };
    const auto reader = [&] {
        while (!start.load())
            QThread::yieldCurrentThread();
        for (int i = 0; i < 250; ++i)
            history.trackedExecutables();
    };
    QFuture<void> w = QtConcurrent::run(writer);
    QFuture<void> r = QtConcurrent::run(reader);
    start = true;
    w.waitForFinished();
    r.waitForFinished();

    // Every increment survived the interleaving: 250 records, no torn reads.
    QCOMPARE(history.launchCount(path), 250);
    QCOMPARE(history.trackedExecutables().size(), 1);
    QCOMPARE(history.trackedExecutables().at(0).targetPath, path);
}

QTEST_MAIN(TstHistory)
#include "tst_history.moc"
