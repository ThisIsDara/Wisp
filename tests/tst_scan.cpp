#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QtTest>

#include <windows.h>

#include <atomic>
#include <memory>

#include "core/FileIndex.h"
#include "core/ScanService.h"
#include "win/WinDirectoryWalk.h"

// Scan seam contract (07-01 task 3): FileIndex wired to the REAL
// WinDirectoryWalk (FindFirstFileExW) against a QTemporaryDir fixture —
// end-to-end proof that the walker's attributes reach the index filter, the
// D-06 skip list works on real trees, hidden/system bits are honored via the
// Win32 attribute path, persistence round-trips with real mtimes, and the
// index never crashes on garbage files (D-07 corruption contract).
class TstScan : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void realWalkIndexesExesAndFolders();
    void skipListSkipsRealSubtrees();
    void hiddenFileExcluded();
    void persistThenRequery();
    void corruptIndexFileTolerated();

    // 07-03 ScanService orchestration: single-flight + coalesce, state
    // machine, UI-thread snapshot discipline, no-scan-at-boot, summary.
    void scanPopulatesIndexAndSummary();
    void singleFlightCoalescesConcurrentScans();
    void noRootsClearsIndexAndState();
    void failedListingMapsErrorState();
    void snapshotReadOnUiThread_Pitfall4();
    void startArmsTimerOnlyWithRoots();
    void refreshIntervalReadsFreshSnapshot();

private:
    // Recreated per test — fixtures must never leak between cases.
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_root;
    QString m_indexPath;

    // WR-05 convention: dedicated pool — controlled threads, zero contention
    // on the shared global QtConcurrent pool.
    QThreadPool m_pool;
    static constexpr int kWaitGenerous = 5000; // QSignalSpy::wait timeout — load-proof
};

void TstScan::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_root = FileIndex::normalize(m_dir->path());
    m_indexPath = m_dir->filePath(QStringLiteral("index.dat"));
}

void TstScan::realWalkIndexesExesAndFolders()
{
    QVERIFY(QDir().mkpath(m_root + QStringLiteral("\\sub")));
    auto writeFile = [](const QString &path, const QByteArray &data) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(data);
        return true;
    };
    QVERIFY(writeFile(m_root + QStringLiteral("\\app.exe"), "MZ"));
    QVERIFY(writeFile(m_root + QStringLiteral("\\sub\\tool.exe"), "MZ"));
    QVERIFY(writeFile(m_root + QStringLiteral("\\sub\\lib.dll"), "junk"));
    QVERIFY(writeFile(m_root + QStringLiteral("\\readme.txt"), "not indexed"));

    FileIndex index(m_indexPath);
    const auto outcome = index.walkAndDelta(QStringList{m_root}, WinDirectoryWalk::winListDirectory);
    QCOMPARE(outcome.failedListings, 0);
    QCOMPARE(outcome.dirsListed, 2);

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 3); // app.exe + sub + sub\tool.exe

    const auto exes = index.queryCandidates(QStringLiteral("tool"));
    QCOMPARE(exes.size(), 1);
    QCOMPARE(exes.at(0).path, m_root + QStringLiteral("\\sub\\tool.exe"));
    QVERIFY(!exes.at(0).isFolder);

    // 07-06: folders stay INDEXED (entryCount 3 above — the removal sweep
    // keys off them) but never surface in candidates — the executable
    // launcher shows .exe rows only. "sub" still matches sub\tool.exe
    // (path-wide subsequence); the folder row itself is filtered.
    const auto folder = index.queryCandidates(QStringLiteral("sub"));
    for (const auto &c : folder)
        QVERIFY(!c.isFolder); // no folder rows in list/search
    QCOMPARE(folder.size(), 1); // only sub\tool.exe surfaces
    QCOMPARE(folder.at(0).path, m_root + QStringLiteral("\\sub\\tool.exe"));
}

void TstScan::skipListSkipsRealSubtrees()
{
    const QString junk = m_root + QStringLiteral("\\node_modules");
    QVERIFY(QDir().mkpath(junk));
    QFile f(junk + QStringLiteral("\\dep.exe"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("MZ");
    f.close();

    FileIndex index(m_indexPath);
    const auto outcome = index.walkAndDelta(QStringList{m_root}, WinDirectoryWalk::winListDirectory);
    QCOMPARE(outcome.dirsListed, 1); // root only — node_modules never descended
    QCOMPARE(outcome.added.size(), 0); // nothing indexable at the root
    QVERIFY(index.queryCandidates(QStringLiteral("dep")).isEmpty());
}

void TstScan::hiddenFileExcluded()
{
    QFile f(m_root + QStringLiteral("\\secret.exe"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("MZ");
    f.close();
    // Real Win32 attribute bit — the walker must read it from the find data.
    QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>((m_root + QStringLiteral("\\secret.exe")).utf16()),
                               FILE_ATTRIBUTE_HIDDEN));

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{m_root}, WinDirectoryWalk::winListDirectory));
    QCOMPARE(index.entryCount(), 0);
    QVERIFY(index.queryCandidates(QStringLiteral("secret")).isEmpty());
}

void TstScan::persistThenRequery()
{
    QFile f(m_root + QStringLiteral("\\persist.exe"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("MZ");
    f.close();

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{m_root}, WinDirectoryWalk::winListDirectory));
    QCOMPARE(index.entryCount(), 1);
    QVERIFY(index.save());

    FileIndex reloaded(m_indexPath);
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.entryCount(), 1);
    QCOMPARE(reloaded.queryCandidates(QStringLiteral("persist")).size(), 1);
}

void TstScan::corruptIndexFileTolerated()
{
    QFile f(m_indexPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(4096, '\xff'));
    f.close();

    FileIndex index(m_indexPath);
    QVERIFY(!index.load()); // truncated garbage → false, never a crash
    QCOMPARE(index.entryCount(), 0);
    QVERIFY(index.queryCandidates(QStringLiteral("x")).isEmpty());
}

// ── 07-03 ScanService orchestration ──

namespace {

// Shared fake-tree helper: root with one .exe and one dir; the dir holds a
// non-exe (indexed as a folder entry only) → exactly 2 index entries, no
// failed listings → Idle end-state for the happy path.
QHash<QString, WinDirectoryWalk::WinDirListing> scanFixtureMap(const QString &root)
{
    WinDirectoryWalk::WinDirEntry exe;
    exe.name = QStringLiteral("app.exe");
    exe.lastWriteMs = 10;
    WinDirectoryWalk::WinDirEntry sub;
    sub.name = QStringLiteral("sub");
    sub.isDir = true;
    sub.lastWriteMs = 21;
    WinDirectoryWalk::WinDirListing rootListing;
    rootListing.entries = { exe, sub };
    rootListing.lastWriteMs = 5;
    rootListing.ok = true;

    WinDirectoryWalk::WinDirEntry dll;
    dll.name = QStringLiteral("lib.dll");
    dll.lastWriteMs = 30;
    WinDirectoryWalk::WinDirListing subListing;
    subListing.entries = { dll };
    subListing.lastWriteMs = 21;
    subListing.ok = true;

    return { { root, rootListing }, { root + QStringLiteral("\\sub"), subListing } };
}

// Mutable fake settings — tests mutate it to flip roots between scans.
struct MutableSettingsHolder {
    QStringList roots;
};

} // namespace

void TstScan::scanPopulatesIndexAndSummary()
{
    const QString root = QStringLiteral("C:\\Root");
    auto map = scanFixtureMap(root);
    auto listFn = [&map](const QString &p) { return map.value(p); };

    MutableSettingsHolder settings;
    settings.roots = { root };

    FileIndex index(m_indexPath);
    ScanService service;
    service.setIndex(&index);
    service.setListFn(listFn);
    service.setSettingsSource(
        [&settings] { return ScanService::ScanSettings{ settings.roots, 10 }; });
    service.setPool(&m_pool);

    QSignalSpy spy(&service, &ScanService::scanStateChanged);
    service.requestScan();
    QVERIFY(spy.wait(kWaitGenerous));

    QCOMPARE(index.entryCount(), 2); // app.exe + sub (lib.dll not indexed)
    QCOMPARE(service.stateOrdinal(), int(ScanService::Idle));
    QVERIFY(QRegularExpression(QStringLiteral("^Last scan \\d{2}:\\d{2} — 2 entries$"))
                .match(service.lastScanSummary())
                .hasMatch());
}

void TstScan::singleFlightCoalescesConcurrentScans()
{
    const QString root = QStringLiteral("C:\\Root");
    auto map = scanFixtureMap(root);
    std::atomic<int> rootCalls{ 0 };
    std::atomic<int> subCalls{ 0 };
    auto listFn = [&map, &rootCalls, &subCalls](const QString &p) {
        if (p.endsWith(QStringLiteral("\\sub")))
            subCalls.fetch_add(1);
        else if (rootCalls.fetch_add(1) == 0)
            QThread::msleep(300); // first walk is slow — the second request lands mid-flight
        return map.value(p);
    };
    MutableSettingsHolder settings;
    settings.roots = { root };

    FileIndex index(m_indexPath);
    ScanService service;
    service.setIndex(&index);
    service.setListFn(listFn);
    service.setSettingsSource(
        [&settings] { return ScanService::ScanSettings{ settings.roots, 10 }; });
    service.setPool(&m_pool);

    QSignalSpy spy(&service, &ScanService::scanStateChanged);
    service.requestScan();
    service.requestScan(); // mid-flight → coalesced follow-up, never a queue

    // Walk 1 (slow): root + sub. Coalesced walk 2: root re-listed, sub
    // memo-skipped (unchanged mtimes). ⇒ root==2, sub==1, total==3.
    // A broken single-flight would run walk 2 CONCURRENTLY → root==2,
    // sub==2, total==4. The exact counts discriminate serial coalescing.
    QTRY_COMPARE_WITH_TIMEOUT(rootCalls.load(), 2, kWaitGenerous);
    QTRY_COMPARE_WITH_TIMEOUT(subCalls.load(), 1, kWaitGenerous);
    QTRY_COMPARE_WITH_TIMEOUT(service.stateOrdinal(), int(ScanService::Idle),
                              kWaitGenerous); // final walk fully applied
    QCOMPARE(index.entryCount(), 2); // consistent end state
    QCOMPARE(service.stateOrdinal(), int(ScanService::Idle));
    QVERIFY(spy.count() >= 2); // at least one Scanning→Idle round trip
}

void TstScan::noRootsClearsIndexAndState()
{
    const QString root = QStringLiteral("C:\\Root");
    auto map = scanFixtureMap(root);
    auto listFn = [&map](const QString &p) { return map.value(p); };
    MutableSettingsHolder settings;
    settings.roots = { root };

    FileIndex index(m_indexPath);
    ScanService service;
    service.setIndex(&index);
    service.setListFn(listFn);
    service.setSettingsSource(
        [&settings] { return ScanService::ScanSettings{ settings.roots, 10 }; });
    service.setPool(&m_pool);

    service.requestScan();
    {
        QSignalSpy spy(&service, &ScanService::scanStateChanged);
        QVERIFY(spy.wait(kWaitGenerous)); // wait (generous) for completion
    }
    QCOMPARE(index.entryCount(), 2);

    // Roots removed → the next scan wipes the index (no locations semantics).
    settings.roots.clear();
    QSignalSpy spy(&service, &ScanService::scanStateChanged);
    service.requestScan();
    QVERIFY(spy.wait(kWaitGenerous));

    QCOMPARE(service.stateOrdinal(), int(ScanService::NoRoots));
    QCOMPARE(index.entryCount(), 0);
}

void TstScan::failedListingMapsErrorState()
{
    const QString root = QStringLiteral("C:\\Root");
    auto listFn = [](const QString &) { return WinDirectoryWalk::WinDirListing{}; }; // ok=false
    MutableSettingsHolder settings;
    settings.roots = { root };

    FileIndex index(m_indexPath);
    ScanService service;
    service.setIndex(&index);
    service.setListFn(listFn);
    service.setSettingsSource(
        [&settings] { return ScanService::ScanSettings{ settings.roots, 10 }; });
    service.setPool(&m_pool);

    QSignalSpy spy(&service, &ScanService::scanStateChanged);
    service.requestScan();
    QVERIFY(spy.wait(kWaitGenerous));

    QCOMPARE(service.stateOrdinal(), int(ScanService::Error));
    QVERIFY(service.lastScanSummary().contains(QStringLiteral("1 location failed")));
}

void TstScan::snapshotReadOnUiThread_Pitfall4()
{
    const QString root = QStringLiteral("C:\\Root");
    auto map = scanFixtureMap(root);
    auto listFn = [&map](const QString &p) { return map.value(p); };
    MutableSettingsHolder settings;
    settings.roots = { root };

    std::atomic<Qt::HANDLE> sourceThread{ nullptr };
    FileIndex index(m_indexPath);
    ScanService service;
    service.setIndex(&index);
    service.setListFn(listFn);
    service.setSettingsSource([&settings, &sourceThread] {
        sourceThread.store(QThread::currentThreadId());
        return ScanService::ScanSettings{ settings.roots, 10 };
    });
    service.setPool(&m_pool);

    QSignalSpy spy(&service, &ScanService::scanStateChanged);
    service.requestScan();
    QVERIFY(spy.wait(kWaitGenerous));

    // Pitfall 4: the settings snapshot was read on the UI (test) thread —
    // the worker never touched the source.
    QCOMPARE(sourceThread.load(), QThread::currentThreadId());
    QCOMPARE(index.entryCount(), 2);
}

void TstScan::startArmsTimerOnlyWithRoots()
{
    // (a) empty roots → NoRoots state, no timer, no scan, no crash.
    {
        MutableSettingsHolder settings; // roots empty
        FileIndex index(m_indexPath);
        ScanService service;
        service.setIndex(&index);
        service.setSettingsSource(
            [&settings] { return ScanService::ScanSettings{ settings.roots, 10 }; });
        service.setPool(&m_pool);

        QSignalSpy spy(&service, &ScanService::scanStateChanged);
        service.start();
        QCOMPARE(service.stateOrdinal(), int(ScanService::NoRoots));
        QCOMPARE(spy.count(), 1); // the Idle→NoRoots transition
    }

    // (b) roots present → timer armed, NO scan at boot (D-09): zero listFn
    // calls, state stays Idle (no spurious emit).
    {
        const QString root = QStringLiteral("C:\\Root");
        auto map = scanFixtureMap(root);
        std::atomic<int> listCalls{ 0 };
        auto listFn = [&map, &listCalls](const QString &p) {
            listCalls.fetch_add(1);
            return map.value(p);
        };
        MutableSettingsHolder settings;
        settings.roots = { root };

        FileIndex index(m_indexPath);
        ScanService service;
        service.setIndex(&index);
        service.setListFn(listFn);
        service.setSettingsSource(
            [&settings] { return ScanService::ScanSettings{ settings.roots, 10 }; });
        service.setPool(&m_pool);

        QSignalSpy spy(&service, &ScanService::scanStateChanged);
        service.start();
        QCOMPARE(service.stateOrdinal(), int(ScanService::Idle)); // been Idle the whole time
        QCOMPARE(listCalls.load(), 0); // D-09: no boot scan
        QCOMPARE(spy.count(), 0);      // no spurious NOTIFY
    }
}

void TstScan::refreshIntervalReadsFreshSnapshot()
{
    MutableSettingsHolder settings;
    settings.roots = { QStringLiteral("C:\\Root") };

    std::atomic<int> sourceCalls{ 0 };
    FileIndex index(m_indexPath);
    ScanService service;
    service.setIndex(&index);
    service.setSettingsSource([&settings, &sourceCalls] {
        sourceCalls.fetch_add(1);
        return ScanService::ScanSettings{ settings.roots, 10 };
    });
    service.setPool(&m_pool);

    QSignalSpy spy(&service, &ScanService::scanStateChanged);
    service.refreshInterval();
    service.refreshInterval();
    QCOMPARE(sourceCalls.load(), 2); // fresh snapshot each call
    QCOMPARE(spy.count(), 0);        // state untouched
}

QTEST_MAIN(TstScan)
#include "tst_scan.moc"
