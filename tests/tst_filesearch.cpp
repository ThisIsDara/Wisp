#include <QtTest>

#include <QThread>
#include <QThreadPool>

#include <atomic>

#include "core/AppEntry.h"
#include "core/FileSearch.h"

// QSignalSpy records QVector<AppEntry> signal args — the element metatype must
// be known to the meta-type system (Qt 6 auto-registers containers of it).
Q_DECLARE_METATYPE(AppEntry)

// FileSearch coordinator contract (D-12..D-18): 150ms debounce where the LAST
// text wins (D-12), generation-countered stale-drop (D-15), empty-query bypass
// (D-14), NoRoots/Error skip + Scanning query-and-status (D-16/D-17 spirit,
// 07-04 remap), query-failure → Error (RESEARCH §2), tracked-source merge
// (D-06/D-07), addExecutable immediate re-dispatch (D-11), quiet fill-in
// (D-13). All behaviors proven with injected fakes — zero COM, zero real
// index (RESEARCH Validation Architecture: tst_filesearch).
//
// WR-05: every FileSearch here runs on a DEDICATED QThreadPool (setPool) —
// never the shared global QtConcurrent pool — and delivery-dependent
// assertions use QSignalSpy::wait() with a generous timeout instead of fixed
// wall-clock waits. The pipeline is asserted on its FINAL state (generation,
// query text, entry set), not on real-time completion order; only the
// "nothing fires inside the debounce window" checks keep a short wait (100ms
// against the 150ms timer — the 50ms margin is the D-12/D-13 contract).

namespace {

AppEntry fileEntry(const QString &name, const QString &path)
{
    AppEntry e;
    e.source = AppEntry::Source::File;
    e.displayName = name;
    e.targetPath = path;
    return e;
}

// Hoisted named consts — no braced-init-lists inside QCOMPARE (MSVC rule).
const auto kNoRoots = FileSearch::FileSearchState::NoRoots;
const auto kScanning = FileSearch::FileSearchState::Scanning;
const auto kError = FileSearch::FileSearchState::Error;
const auto kIdle = FileSearch::FileSearchState::Idle;

} // namespace

class TstFileSearch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void debounceFiresOnceAfterQuiet_D12();
    void staleGenerationDropped_D15();
    void staleTextDroppedInDebounceWindow_WR03();
    void emptyQueryAddedOnly_D14();
    void noRootsSkipsQuery_D16();
    void errorSkipsQuery();
    void scanningQueriesAndReports_D17();
    void okClearsStateAndText();
    void queryFailureMapsError();
    void trackedSourceMerged_D06();
    void addExecutableRedispatches_D11();
    void quietFillIn_D13();

private:
    // WR-05: dedicated pool — controlled thread count, zero contention from
    // other suites on the shared global pool. Outlives every local FileSearch.
    QThreadPool m_pool;
    static constexpr int kQuietMs = 100;      // inside the 150ms debounce window
    static constexpr int kWaitMs = 200;       // comfortably past the debounce
    static constexpr int kWaitGenerous = 5000; // QSignalSpy::wait timeout — load-proof
};

void TstFileSearch::initTestCase()
{
    qRegisterMetaType<QVector<AppEntry>>(); // QSignalSpy arg conversion (no-op if known)
}

void TstFileSearch::debounceFiresOnceAfterQuiet_D12()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    std::atomic<int> calls{ 0 };
    QString lastArg;
    fs.setQueryFn([&](const QString &q) {
        ++calls;
        lastArg = q;
        return FileSearch::QueryResult{};
    });
    // statusFn unset → no-op default → treated as Ok (query runs)
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    fs.setQuery(QStringLiteral("a"));
    QTest::qWait(kQuietMs);
    QCOMPARE(calls.load(), 0); // inside the debounce window — nothing fired

    fs.setQuery(QStringLiteral("ab"));
    QVERIFY(results.wait(kWaitGenerous)); // the single delivery lands (WR-05: wait, not wall clock)
    QCOMPARE(calls.load(), 1);       // exactly ONE query after typing pauses (D-12)
    QCOMPARE(results.count(), 1);
    QCOMPARE(lastArg, QStringLiteral("ab")); // the LAST text wins
}

void TstFileSearch::staleGenerationDropped_D15()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    fs.setQueryFn([](const QString &q) {
        if (q == QLatin1String("a"))
            QThread::msleep(300); // dispatch 1 is slow — completes AFTER dispatch 2
        return FileSearch::QueryResult{
            { fileEntry(QStringLiteral("B.exe"), QStringLiteral("C:\\b\\B.exe")) }, false
        };
    });
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    fs.setQuery(QStringLiteral("a"));
    QTest::qWait(kWaitMs);  // dispatch 1 starts (worker sleeps 300ms)
    fs.setQuery(QStringLiteral("b"));
    // WR-05: wait for the delivery instead of a fixed 200ms window — on a
    // loaded machine the fast worker may be delayed; the generation guard
    // (NOT wall-clock ordering) is what makes this deterministic.
    QVERIFY(results.wait(kWaitGenerous));
    QTest::qWait(600);      // dispatch 1 completes → stale → dropped (D-15)

    QCOMPARE(results.count(), 1); // exactly ONE delivery — stale "a" never surfaces
    QCOMPARE(results.first().at(0).toULongLong(), quint64(2)); // generation == latest
    QCOMPARE(results.first().at(1).toString(), QStringLiteral("b")); // WR-03: the query text travels
    const QVector<AppEntry> files = results.first().at(2).value<QVector<AppEntry>>();
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).displayName, QStringLiteral("B.exe")); // entries from "b"
}

void TstFileSearch::staleTextDroppedInDebounceWindow_WR03()
{
    // WR-03: the generation guard proves recency, not relevance. Dispatch 1
    // runs for "abc"; the user types "def" while it is in flight; dispatch 1
    // completes INSIDE the new debounce window (before "def" dispatches) —
    // its generation is still current, but its TEXT is stale. The coordinator
    // must drop it; only the "def" result may surface.
    FileSearch fs;
    fs.setPool(&m_pool);
    std::atomic<bool> releaseFirst{ false };
    fs.setQueryFn([&](const QString &q) {
        if (q == QLatin1String("abc")) {
            while (!releaseFirst.load()) // worker blocks until the UI thread lets it finish
                QThread::msleep(5);
        }
        return FileSearch::QueryResult{
            { fileEntry(QStringLiteral("A.exe"), QStringLiteral("C:\\a\\A.exe")) }, false
        };
    });
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    fs.setQuery(QStringLiteral("abc"));
    QTest::qWait(kWaitMs);  // dispatch 1 fired (~150ms) — worker blocked on the latch
    fs.setQuery(QStringLiteral("def")); // debounce restarts — dispatch 2 is ~150ms away
    QTest::qWait(kQuietMs); // still inside the new debounce window
    releaseFirst = true;    // dispatch 1 completes NOW — before dispatch 2 fires

    QVERIFY(results.wait(kWaitGenerous)); // "def" dispatches and delivers — the ONLY delivery
    QCOMPARE(results.count(), 1); // stale-text "abc" never surfaced (WR-03)
    QCOMPARE(results.first().at(0).toULongLong(), quint64(2)); // generation == latest
    QCOMPARE(results.first().at(1).toString(), QStringLiteral("def")); // only "def" rows
    const QVector<AppEntry> files = results.first().at(2).value<QVector<AppEntry>>();
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).displayName, QStringLiteral("A.exe")); // both dispatches return the same row
}

void TstFileSearch::emptyQueryAddedOnly_D14()
{
    // D-14 (default list): empty query DISPATCHES an added-only snapshot —
    // the curated catalog renders via the app pipeline, manual picks flow
    // here. No index QueryFn call, and the delivery arrives instantly.
    FileSearch fs;
    fs.setPool(&m_pool);
    std::atomic<int> calls{ 0 };
    fs.setQueryFn([&](const QString &) {
        ++calls;
        return FileSearch::QueryResult{};
    });
    std::atomic<int> addedCalls{ 0 };
    fs.setAddedSource([&] {
        ++addedCalls;
        return QVector<AppEntry>{ fileEntry(QStringLiteral("A.exe"), QStringLiteral("C:\\a\\A.exe")) };
    });
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    fs.setQuery(QString());
    QVERIFY(results.wait(kWaitGenerous)); // the added-only snapshot delivers
    QCOMPARE(calls.load(), 0);            // D-14: no index query on empty
    QCOMPARE(addedCalls.load(), 1);
    QCOMPARE(results.count(), 1);
    QCOMPARE(results.first().at(1).toString(), QString()); // carries the empty query
    const QVector<AppEntry> files = results.first().at(2).value<QVector<AppEntry>>();
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).displayName, QStringLiteral("A.exe"));
}

void TstFileSearch::noRootsSkipsQuery_D16()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    std::atomic<int> calls{ 0 };
    fs.setQueryFn([&](const QString &) {
        ++calls;
        return FileSearch::QueryResult{};
    });
    fs.setStatusFn([]() { return static_cast<int>(kNoRoots); });
    QSignalSpy results(&fs, &FileSearch::resultsReady);
    QSignalSpy changed(&fs, &FileSearch::stateChanged);

    fs.setQuery(QStringLiteral("x"));
    QVERIFY(results.wait(kWaitGenerous));

    QCOMPARE(calls.load(), 0); // D-16: NoRoots skips the query entirely
    QCOMPARE(results.count(), 1); // status-only delivery (empty files)
    QCOMPARE(changed.count(), 1); // Idle → NoRoots transition surfaced once
    QCOMPARE(fs.statusText(),
             QStringLiteral("No scan locations yet — add folders in Settings to search files"));
    QCOMPARE(fs.indexerOk(), false);
}

void TstFileSearch::errorSkipsQuery()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    std::atomic<int> calls{ 0 };
    fs.setQueryFn([&](const QString &) {
        ++calls;
        return FileSearch::QueryResult{};
    });
    fs.setStatusFn([]() { return static_cast<int>(kError); });
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    fs.setQuery(QStringLiteral("x"));
    QVERIFY(results.wait(kWaitGenerous));

    QCOMPARE(calls.load(), 0); // Error skips the query (status only)
    QCOMPARE(results.count(), 1);
    QCOMPARE(fs.statusText(), QStringLiteral("Scan unavailable — check your scan locations in Settings"));
    QCOMPARE(fs.indexerOk(), false);
}

void TstFileSearch::scanningQueriesAndReports_D17()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    std::atomic<int> calls{ 0 };
    fs.setQueryFn([&](const QString &) {
        ++calls;
        return FileSearch::QueryResult{
            { fileEntry(QStringLiteral("Notepad.exe"), QStringLiteral("C:\\Windows\\notepad.exe")) },
            false
        };
    });
    fs.setStatusFn([]() { return static_cast<int>(kScanning); });
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    fs.setQuery(QStringLiteral("note"));
    QVERIFY(results.wait(kWaitGenerous));

    QCOMPARE(calls.load(), 1); // Scanning still queries (partial results fine — D-17 spirit)
    QCOMPARE(results.count(), 1);
    const QVector<AppEntry> files = results.first().at(2).value<QVector<AppEntry>>();
    QCOMPARE(files.size(), 1); // resultsReady carries the entries
    QCOMPARE(fs.statusText(), QStringLiteral("Scanning — files appear as they're found"));
    QCOMPARE(fs.indexerOk(), false);
}

void TstFileSearch::okClearsStateAndText()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    fs.setQueryFn([](const QString &) {
        return FileSearch::QueryResult{
            { fileEntry(QStringLiteral("Calc.exe"), QStringLiteral("C:\\x\\Calc.exe")) }, false
        };
    });
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    // NoRoots cycle first — the status row must be showing.
    fs.setStatusFn([]() { return static_cast<int>(kNoRoots); });
    fs.setQuery(QStringLiteral("calc"));
    QVERIFY(results.wait(kWaitGenerous));
    QCOMPARE(fs.statusText().isEmpty(), false);
    QCOMPARE(fs.indexerOk(), false);

    // Roots configured → Idle clears the text and re-opens the file pipeline.
    fs.setStatusFn([]() { return static_cast<int>(kIdle); });
    fs.setQuery(QStringLiteral("calc"));
    QVERIFY(results.wait(kWaitGenerous));
    QCOMPARE(fs.statusText(), QString());
    QCOMPARE(fs.indexerOk(), true);
    QCOMPARE(results.count(), 2);
}

void TstFileSearch::queryFailureMapsError()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    fs.setStatusFn([]() { return static_cast<int>(kIdle); });
    fs.setQueryFn([](const QString &) {
        return FileSearch::QueryResult{ QVector<AppEntry>{}, true }; // RESEARCH §2 failed
    });
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    fs.setQuery(QStringLiteral("x"));
    QVERIFY(results.wait(kWaitGenerous));

    QCOMPARE(fs.statusText(), QStringLiteral("Scan unavailable — check your scan locations in Settings"));
    QCOMPARE(fs.indexerOk(), false);
    QCOMPARE(results.count(), 1); // status-only delivery
}

void TstFileSearch::trackedSourceMerged_D06()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    const AppEntry tracked = fileEntry(QStringLiteral("World of Warcraft"),
                                       QStringLiteral("D:\\Games\\WoW.exe"));
    const AppEntry indexRow = fileEntry(QStringLiteral("WinRAR.exe"),
                                        QStringLiteral("C:\\Apps\\WinRAR.exe"));
    fs.setQueryFn([indexRow](const QString &) {
        return FileSearch::QueryResult{ { indexRow }, false };
    });
    fs.setTrackedSource([tracked]() { return QVector<AppEntry>{ tracked }; });
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    // "wow" subsequence-matches the tracked NAME (and path) → merged in (D-06).
    fs.setQuery(QStringLiteral("wow"));
    QVERIFY(results.wait(kWaitGenerous));
    QCOMPARE(results.count(), 1);
    QVector<AppEntry> files = results.first().at(2).value<QVector<AppEntry>>();
    QCOMPARE(files.size(), 2); // index row + tracked entry
    QStringList names;
    for (const AppEntry &e : files)
        names << e.displayName;
    QCOMPARE(names.contains(QStringLiteral("World of Warcraft")), true);

    // "zzz" matches neither name nor path → tracked entry excluded (D-07).
    fs.setQuery(QStringLiteral("zzz"));
    QVERIFY(results.wait(kWaitGenerous));
    QCOMPARE(results.count(), 2);
    files = results.last().at(2).value<QVector<AppEntry>>();
    QCOMPARE(files.size(), 1); // index row only
    QCOMPARE(files.at(0).displayName, QStringLiteral("WinRAR.exe"));
}

void TstFileSearch::addExecutableRedispatches_D11()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    std::atomic<int> calls{ 0 };
    fs.setQueryFn([&](const QString &) {
        ++calls;
        return FileSearch::QueryResult{};
    });
    QString stored;
    fs.setAddExeDialog([]() { return QStringLiteral("D:\\Games\\App.exe"); });
    fs.setAddEntryStore([&](const QString &p) { stored = p; });
    QSignalSpy done(&fs, &FileSearch::addExecutableDone);
    QSignalSpy results(&fs, &FileSearch::resultsReady);

    fs.setQuery(QStringLiteral("app"));
    QVERIFY(results.wait(kWaitGenerous));
    QCOMPARE(calls.load(), 1);

    fs.addExecutable();
    QCOMPARE(stored, QStringLiteral("D:\\Games\\App.exe")); // dialog → store (D-11)
    QCOMPARE(done.count(), 1);                              // addExecutableDone emitted
    // The re-dispatch is SYNCHRONOUS (dispatch() inside addExecutable) — both
    // the worker call and its delivery land inside this window, which is well
    // INSIDE the 150ms debounce. A debounced re-dispatch could not have fired
    // yet. (QSignalSpy::wait() would not help here: it only waits for a NEW
    // signal — the delivery has already been recorded by the time it runs.)
    QTest::qWait(kQuietMs);
    QCOMPARE(calls.load(), 2); // D-11: a SECOND dispatch fired, no debounce wait
    QCOMPARE(results.count(), 2); // ...and its delivery landed in the same window
}

void TstFileSearch::quietFillIn_D13()
{
    FileSearch fs;
    fs.setPool(&m_pool);
    fs.setQueryFn([](const QString &) {
        return FileSearch::QueryResult{
            { fileEntry(QStringLiteral("X.exe"), QStringLiteral("C:\\x\\X.exe")) }, false
        };
    });
    QSignalSpy results(&fs, &FileSearch::resultsReady);
    QSignalSpy changed(&fs, &FileSearch::stateChanged);

    fs.setQuery(QStringLiteral("x"));
    QTest::qWait(kQuietMs); // inside the 150ms window

    QCOMPARE(results.count(), 0); // quiet fill-in (D-13): nothing fires early
    QCOMPARE(changed.count(), 0); // no indicator signals exist by construction
}

QTEST_MAIN(TstFileSearch)
#include "tst_filesearch.moc"
