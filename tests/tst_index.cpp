#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <thread>

#include "core/FileIndex.h"
#include "win/WinDirectoryWalk.h"

// FileIndex delta contract (07-01 task 3): pure walkAndDelta logic driven by
// a FAKE listing map — no real files, no Win32. Covers the D-08 incremental
// memo (unchanged subtree → zero list calls), per-dir mtime deltas (added /
// removed / renamed), subtree sweeps on dir deletion, failure tolerance
// (ok=false → old data kept, failedListings counted), the D-06 skip list and
// D-02 .exe+folders filter through the walker, cap-100 prefilter, and a
// cross-thread smoke test (walkAndDelta + queryCandidates under the mutex).
class TstIndex : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void initialWalkIndexesExesAndFolders();
    void incrementalSkipsUnchangedSubtrees();
    void mtimeChangeAddsRemovesAndRenames();
    void dirDeletionSweepsSubtree();
    void failedListingKeepsOldData();
    void skipListBlocksSubtrees();
    void hiddenEntriesAreExcluded();
    void queryCandidatesPrefilter();
    void persistenceRoundTrip();
    void saveCreatesMissingParentDir();
    void corruptIndexFileLoadsEmpty();
    void wipeThenRescanRepopulates();
    void toEntriesStripsExe_20260815();
    void concurrentReadsDuringWalk();

private:
    WinDirectoryWalk::WinDirEntry entry(const QString &name, bool dir, qint64 mtime,
                                        bool hidden = false) const
    {
        WinDirectoryWalk::WinDirEntry e;
        e.name = name;
        e.isDir = dir;
        e.hidden = hidden;
        e.lastWriteMs = mtime;
        return e;
    }

    WinDirectoryWalk::WinDirListing listing(QVector<WinDirectoryWalk::WinDirEntry> entries,
                                            qint64 mtime) const
    {
        WinDirectoryWalk::WinDirListing l;
        l.entries = entries;
        l.lastWriteMs = mtime;
        l.ok = true;
        return l;
    }

    // One-armed fake: roots resolve, everything else fails cleanly.
    std::function<WinDirectoryWalk::WinDirListing(const QString &)> fakeList;
    QTemporaryDir m_dir;
    QString m_indexPath;
};

void TstIndex::init()
{
    QVERIFY(m_dir.isValid());
    m_indexPath = m_dir.filePath(QStringLiteral("index.dat"));
    fakeList = [](const QString &) { return WinDirectoryWalk::WinDirListing{}; }; // ok=false
}

void TstIndex::initialWalkIndexesExesAndFolders()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("app.exe"), false, 10), entry(QStringLiteral("sub"), true, 21)},
                       5)},
        {root + QStringLiteral("\\sub"),
         listing({entry(QStringLiteral("tool.exe"), false, 30), entry(QStringLiteral("lib.dll"), false, 31)},
                 21)},
    };
    auto listFn = [&map](const QString &p) {
        return map.value(p); // missing → default ok=false
    };

    FileIndex index(m_indexPath);
    const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
    QCOMPARE(outcome.failedListings, 0);
    QCOMPARE(outcome.dirsListed, 2);
    QCOMPARE(outcome.added.size(), 3); // app.exe + sub + sub\tool.exe

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 3);

    const auto candidates = index.queryCandidates(QStringLiteral("tool"));
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.at(0).path, root + QStringLiteral("\\sub\\tool.exe"));
    QVERIFY(!candidates.at(0).isFolder);
}

void TstIndex::incrementalSkipsUnchangedSubtrees()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("app.exe"), false, 10), entry(QStringLiteral("sub"), true, 21)},
                       5)},
        {root + QStringLiteral("\\sub"), listing({entry(QStringLiteral("tool.exe"), false, 30)}, 21)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));
    QCOMPARE(index.entryCount(), 3);

    // Second walk, nothing changed: ONLY the root is re-listed — the sub
    // subtree's memo mtime matches its parent-listing mtime → zero descent.
    // (Root's own children are re-verified each walk → 2 adds, deduped.)
    const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
    QCOMPARE(outcome.dirsListed, 1);
    QCOMPARE(outcome.added.size(), 2); // app.exe + sub re-added by the root re-list
    QCOMPARE(outcome.removed.size(), 0);

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 3); // no churn on unchanged trees
}

void TstIndex::mtimeChangeAddsRemovesAndRenames()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("app.exe"), false, 10), entry(QStringLiteral("sub"), true, 21)},
                       5)},
        {root + QStringLiteral("\\sub"),
         listing({entry(QStringLiteral("tool.exe"), false, 30), entry(QStringLiteral("gone.exe"), false, 31)}, 21)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));

    // sub changes: tool.exe + gone.exe deleted, fresh.exe added. Root:
    // app.exe renamed away. The parent-listing view of sub's mtime (99)
    // must match sub's own fresh listing mtime (99) — both are the same
    // directory FILETIME in reality.
    map[root + QStringLiteral("\\sub")] = listing({entry(QStringLiteral("fresh.exe"), false, 40)}, 99);
    map[root] = listing({entry(QStringLiteral("renamed.exe"), false, 11), entry(QStringLiteral("sub"), true, 99)},
                        6);

    const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
    QCOMPARE(outcome.dirsListed, 2);

    // Removed: app.exe (renamed away), sub\tool.exe + sub\gone.exe
    // (deleted). Added: the fresh ones. Rename = removed(old) + added(new)
    // — never a stale row.
    QVERIFY(outcome.removed.contains(FileIndex::normalize(root + QStringLiteral("\\app.exe")).toCaseFolded()));
    QVERIFY(outcome.removed.contains(FileIndex::normalize(root + QStringLiteral("\\sub\\tool.exe")).toCaseFolded()));
    QVERIFY(outcome.removed.contains(FileIndex::normalize(root + QStringLiteral("\\sub\\gone.exe")).toCaseFolded()));
    QVERIFY(!outcome.removed.contains(FileIndex::normalize(root + QStringLiteral("\\sub")).toCaseFolded())); // sub survives

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 3); // renamed.exe + sub + sub\fresh.exe
    const auto candidates = index.queryCandidates(QStringLiteral("fresh"));
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.at(0).path, root + QStringLiteral("\\sub\\fresh.exe"));
}

void TstIndex::dirDeletionSweepsSubtree()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("app.exe"), false, 10), entry(QStringLiteral("sub"), true, 21)},
                       5)},
        {root + QStringLiteral("\\sub"), listing({entry(QStringLiteral("tool.exe"), false, 30), entry(QStringLiteral("deep"), true, 22)}, 21)},
        {root + QStringLiteral("\\sub\\deep"), listing({entry(QStringLiteral("x.exe"), false, 40)}, 22)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));
    QCOMPARE(index.entryCount(), 5); // app.exe + sub + tool.exe + deep + x.exe (deep is indexed as a dir)

    // sub is gone from the root listing → its whole subtree must vanish.
    map[root] = listing({entry(QStringLiteral("app.exe"), false, 10)}, 6);
    map.remove(root + QStringLiteral("\\sub"));
    map.remove(root + QStringLiteral("\\sub\\deep"));

    const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
    QVERIFY(outcome.removed.contains(FileIndex::normalize(root + QStringLiteral("\\sub")).toCaseFolded()));
    QVERIFY(outcome.removed.contains(FileIndex::normalize(root + QStringLiteral("\\sub\\tool.exe")).toCaseFolded()));
    QVERIFY(outcome.removed.contains(FileIndex::normalize(root + QStringLiteral("\\sub\\deep\\x.exe")).toCaseFolded()));

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 1); // only app.exe remains
    QVERIFY(index.queryCandidates(QStringLiteral("deep")).isEmpty());
}

void TstIndex::failedListingKeepsOldData()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("app.exe"), false, 10)}, 5)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));
    QCOMPARE(index.entryCount(), 1);

    // Root listing fails (access denied, missing drive, ...): old data must
    // survive untouched, the failure counted for the scan summary.
    map.remove(root);
    const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
    QCOMPARE(outcome.failedListings, 1);
    QCOMPARE(outcome.dirsListed, 0);

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 1);
    QCOMPARE(index.queryCandidates(QStringLiteral("app")).size(), 1);
}

void TstIndex::skipListBlocksSubtrees()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("node_modules"), true, 20), entry(QStringLiteral(".git"), true, 21),
                        entry(QStringLiteral("windows"), true, 22), entry(QStringLiteral("ok.exe"), false, 10)},
                       5)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
    QCOMPARE(outcome.added.size(), 1); // ok.exe ONLY — skipped dirs never descend
    QCOMPARE(outcome.dirsListed, 1);

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 1);
}

void TstIndex::hiddenEntriesAreExcluded()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("vis.exe"), false, 10), entry(QStringLiteral("hid.exe"), false, 11, true),
                        entry(QStringLiteral("secret"), true, 12, true)},
                       5)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
    QCOMPARE(outcome.added.size(), 1);
    QCOMPARE(outcome.dirsListed, 1);
}

void TstIndex::queryCandidatesPrefilter()
{
    const QString root = QStringLiteral("C:\\root");
    QVector<WinDirectoryWalk::WinDirEntry> entries;
    for (int i = 0; i < 150; ++i)
        entries.append(entry(QStringLiteral("App%1.exe").arg(i), false, 10));
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing(entries, 5)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));
    QCOMPARE(index.entryCount(), 150);

    // Cap: 1000 at most, never more (raised 07-06 — the default list IS the
    // index now); 150 entries all fit.
    const auto all = index.queryCandidates(QStringLiteral("app"));
    QCOMPARE(all.size(), 150);

    // 07-06: empty query = the FULL executable default list (all .exe rows).
    QCOMPARE(index.queryCandidates(QString()).size(), 150);

    // Subsequence semantics: "a2" matches App2.exe AND App12.exe, App20-29,
    // App32... (subsequence, not substring) — assert membership, not count.
    const auto sub = index.queryCandidates(QStringLiteral("a2"));
    QVERIFY(sub.size() >= 1);
    bool foundApp2 = false;
    for (const auto &e : sub)
        if (e.path == root + QStringLiteral("\\App2.exe"))
            foundApp2 = true;
    QVERIFY(foundApp2);
}

void TstIndex::persistenceRoundTrip()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("app.exe"), false, 10), entry(QStringLiteral("sub"), true, 21)},
                       5)},
        {root + QStringLiteral("\\sub"), listing({entry(QStringLiteral("tool.exe"), false, 30)}, 21)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));
    QCOMPARE(index.entryCount(), 3);
    QVERIFY(index.save());

    // Fresh instance from the SAME file: the memo must survive too — a
    // second walk with unchanged files must NOT re-descent (incrementality
    // across restarts, D-08).
    FileIndex reloaded(m_indexPath);
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.entryCount(), 3);
    const auto outcome = reloaded.walkAndDelta(QStringList{root}, listFn);
    QCOMPARE(outcome.dirsListed, 1);
}

void TstIndex::saveCreatesMissingParentDir()
{
    // 07-06: QSaveFile does NOT create parent directories — %APPDATA%\TID\
    // wisp\ only materializes on the first save. Save must mkpath first, or
    // every scan's persistence fails silently and the index vanishes on
    // relaunch (the observed bug: roots existed, search worked, no index
    // file on disk).
    const QString nested = m_dir.path() + QStringLiteral("\\a\\b"); // missing
    FileIndex index(nested + QStringLiteral("\\index.dat"));
    index.apply(FileIndex::WalkOutcome{}); // empty outcome — save() still must work
    QVERIFY(index.save());
    QVERIFY(QFileInfo::exists(nested + QStringLiteral("\\index.dat")));
    QVERIFY(FileIndex(nested + QStringLiteral("\\index.dat")).load());
}

void TstIndex::corruptIndexFileLoadsEmpty()
{
    QFile file(m_indexPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("definitely-not-a-wisp-index\xff\xfe\x00"));
    file.close();

    FileIndex index(m_indexPath);
    QVERIFY(!index.load()); // corrupt → false, index stays empty, no crash
    QCOMPARE(index.entryCount(), 0);
}

void TstIndex::wipeThenRescanRepopulates()
{
    // 07-06 regression: an empty-roots scan wipes ALL entries AND the memo.
    // apply() used to insert-accumulate mtimes, so the memo survived the
    // wipe; a re-added root then saw every subtree memo-matching as
    // "unchanged" and skipped recursion — the index stayed at the root's
    // direct children only (observed: 7 entries, 4414 stale mtimes).
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("app.exe"), false, 10), entry(QStringLiteral("sub"), true, 21)},
                       5)},
        {root + QStringLiteral("\\sub"),
         listing({entry(QStringLiteral("tool.exe"), false, 30)}, 21)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));
    QCOMPARE(index.entryCount(), 3);

    // Wipe: no roots → everything removed, memo cleared.
    index.apply(index.walkAndDelta(QStringList{}, listFn));
    QCOMPARE(index.entryCount(), 0);

    // Root re-added (user picked the folder again): the walk must descend
    // INTO sub (memo was cleared) — the deep tool.exe comes back, proving
    // the wipe actually cleared the memo (stale-memo regression).
    const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
    QCOMPARE(outcome.dirsListed, 2); // root + sub — no memo shortcut
    QCOMPARE(outcome.added.size(), 3);
    index.apply(outcome);
    QCOMPARE(index.entryCount(), 3);
    QCOMPARE(index.queryCandidates(QStringLiteral("tool")).size(), 1);
}

void TstIndex::toEntriesStripsExe_20260815()
{
    // 2026-08-15: File-row titles drop the ".exe" extension — the default
    // list shows "Wow", not "Wow.exe" (the user-facing title change). Only
    // .exe is stripped, case-insensitively, from the basename; non-.exe names
    // and folder rows keep their filename. targetPath is never touched.
    const QVector<FileIndex::IndexEntry> candidates = {
        { QStringLiteral("C:\\Games\\WoW.exe"), {}, false },
        { QStringLiteral("C:\\tools\\Tool.EXE"), {}, false },
        { QStringLiteral("C:\\docs\\report.pdf"), {}, false },
        { QStringLiteral("C:\\Games"), {}, true },
    };
    const QVector<AppEntry> out = FileIndex::toEntries(candidates, 10);
    QCOMPARE(out.size(), 4);
    QCOMPARE(out.at(0).displayName, QStringLiteral("WoW"));
    QCOMPARE(out.at(1).displayName, QStringLiteral("Tool"));   // case-insensitive
    QCOMPARE(out.at(2).displayName, QStringLiteral("report.pdf")); // non-.exe kept
    QCOMPARE(out.at(3).displayName, QStringLiteral("Games"));  // folder basename kept
    QCOMPARE(out.at(0).source, AppEntry::Source::File);
    QCOMPARE(out.at(0).targetPath, QStringLiteral("C:\\Games\\WoW.exe")); // path intact
    QCOMPARE(out.at(3).isFolder, true);
}

void TstIndex::concurrentReadsDuringWalk()
{
    const QString root = QStringLiteral("C:\\root");
    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing({entry(QStringLiteral("app.exe"), false, 10), entry(QStringLiteral("sub"), true, 20)},
                       5)},
        {root + QStringLiteral("\\sub"), listing({entry(QStringLiteral("tool.exe"), false, 30)}, 21)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));

    // Reader thread hammers queryCandidates while the main thread walks —
    // both take the same mutex; the walk must remain coherent (no crash,
    // no torn state).
    std::atomic<bool> stop{false};
    std::thread reader([&] {
        int queries = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            index.queryCandidates(QStringLiteral("tool"));
            ++queries;
        }
        return queries;
    });

    for (int i = 0; i < 50; ++i) {
        const auto outcome = index.walkAndDelta(QStringList{root}, listFn);
        index.apply(outcome);
    }
    stop.store(true);
    reader.join();
    QCOMPARE(index.entryCount(), 3);
}

QTEST_MAIN(TstIndex)
#include "tst_index.moc"
