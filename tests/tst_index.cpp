#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <thread>

#include "core/FileIndex.h"
#include "core/FileProvider.h"
#include "core/AppProvider.h"
#include "core/CalculatorProvider.h"
#include "core/CommandProvider.h"
#include <QThreadPool>
#include "core/ResultsModel.h"
#include "win/WinDirectoryWalk.h"

// FileIndex delta contract (07-01 task 3): pure walkAndDelta logic driven by
// a FAKE listing map — no real files, no Win32. Covers the D-08 incremental
// memo (unchanged subtree → zero list calls), per-dir mtime deltas (added /
// removed / renamed), subtree sweeps on dir deletion, failure tolerance
// (ok=false → old data kept, failedListings counted), the D-06 skip list and
// D-02 .exe+.lnk+folders filter through the walker, cap-100 prefilter, and a
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
    void nonMatchingNameNotAdmitted_20260915();
    void duplicateSurvivesProviderPath_20261008();
    void duplicateSurvivesTabSwitch_20261008();

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
        {root, listing({entry(QStringLiteral("app.exe"), false, 10), entry(QStringLiteral("sub"), true, 21),
                        entry(QStringLiteral("game.lnk"), false, 12)},
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
    QCOMPARE(outcome.added.size(), 4); // app.exe + game.lnk + sub + sub\tool.exe

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 4);

    const auto candidates = index.queryCandidates(QStringLiteral("tool"));
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.at(0).path, root + QStringLiteral("\\sub\\tool.exe"));
    QVERIFY(!candidates.at(0).isFolder);

    // 2026-08-15: .lnk shortcuts join the inventory (D-02 extension) —
    // "game.lnk" is indexed and searchable like an .exe.
    const auto lnk = index.queryCandidates(QStringLiteral("game"));
    QCOMPARE(lnk.size(), 1);
    QCOMPARE(lnk.at(0).path, root + QStringLiteral("\\game.lnk"));
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
    // 2026-08-15: File-row titles drop the ".exe"/".lnk" extension — the
    // default list shows "Wow", not "Wow.exe", and "Steam", not "Steam.lnk"
    // (the user-facing title change). Only those two extensions are stripped,
    // case-insensitively, from the basename; other names and folder rows keep
    // their filename. targetPath is never touched.
    const QVector<FileIndex::IndexEntry> candidates = {
        { QStringLiteral("C:\\Games\\WoW.exe"), {}, false },
        { QStringLiteral("C:\\tools\\Tool.EXE"), {}, false },
        { QStringLiteral("C:\\Games\\Steam.lnk"), {}, false },
        { QStringLiteral("C:\\docs\\report.pdf"), {}, false },
        { QStringLiteral("C:\\Games"), {}, true },
    };
    const QVector<AppEntry> out = FileIndex::toEntries(candidates, 10);
    QCOMPARE(out.size(), 5);
    QCOMPARE(out.at(0).displayName, QStringLiteral("WoW"));
    QCOMPARE(out.at(1).displayName, QStringLiteral("Tool"));   // case-insensitive
    QCOMPARE(out.at(2).displayName, QStringLiteral("Steam"));  // .lnk stripped too
    QCOMPARE(out.at(3).displayName, QStringLiteral("report.pdf")); // non-.exe kept
    QCOMPARE(out.at(4).displayName, QStringLiteral("Games"));  // folder basename kept
    QCOMPARE(out.at(0).source, AppEntry::Source::File);
    QCOMPARE(out.at(0).targetPath, QStringLiteral("C:\\Games\\WoW.exe")); // path intact
    QCOMPARE(out.at(4).isFolder, true);
}

void TstIndex::nonMatchingNameNotAdmitted_20260915()
{
    // 2026-09-15 (user report: searching "Discord" also listed "Wow"). A
    // candidate whose PATH does not contain the query as a CONTIGUOUS substring
    // must not reach the path-match tier.
    //
    // The loose subsequence prefilter in queryCandidates() deliberately STAYS —
    // it must remain a superset so genuine name matches are not dropped early.
    // The gate that admits or rejects is FileProvider::pathMatchesQuery(), so
    // that is what this drives, end to end through the real index + provider.
    //
    // The root itself carries the path text, so no subtree listing is needed
    // (the walk skips directories the listing map does not describe).
    const QString root = QStringLiteral("C:\\Tax 2025");
    QVector<WinDirectoryWalk::WinDirEntry> entries;
    entries.append(entry(QStringLiteral("report.exe"), false, 11));   // path hit
    entries.append(entry(QStringLiteral("Summary.exe"), false, 12));  // name hit
    // "Wow.exe" contains no "discord" as text, but d-i-s / c-o-r-d appear IN
    // ORDER across its path, which a subsequence prefilter cannot distinguish.
    entries.append(entry(QStringLiteral("Wow.exe"), false, 13));      // must be rejected

    auto map = QHash<QString, WinDirectoryWalk::WinDirListing>{
        {root, listing(entries, 5)},
    };
    auto listFn = [&map](const QString &p) { return map.value(p); };

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{root}, listFn));

    FileProvider provider;
    provider.setIndex(&index);

    const auto pathsFor = [&provider](const QString &q) {
        QSet<QString> out;
        for (const ScoredEntry &se : provider.query(q, 50, false))
            out.insert(se.entry.targetPath);
        return out;
    };

    // Name match: the query IS the file's title.
    QVERIFY(pathsFor(QStringLiteral("Summary")).contains(root + QStringLiteral("\\Summary.exe")));

    // Path match: "Tax 2025" appears contiguously in the row's own path.
    QVERIFY(pathsFor(QStringLiteral("Tax 2025")).contains(root + QStringLiteral("\\report.exe")));

    // THE REGRESSION: a loose subsequence across the path must not admit Wow.exe.
    const auto dis = pathsFor(QStringLiteral("discord"));
    QVERIFY2(!dis.contains(root + QStringLiteral("\\Wow.exe")),
             "a path that merely contains the query's letters in order must not be admitted");
}

void TstIndex::duplicateSurvivesProviderPath_20261008()
{
    // 2026-10-08 regression, and the reason this test exists.
    //
    // Rows reach the model through TWO different builders:
    //   FileIndex::toEntries  - the default list (empty query)
    //   FileProvider::query   - ANY typed query, via the provider fan-out
    // The first copied the resolved .lnk target; the second silently did not.
    // So with a query typed, every row had an empty launchTarget, duplicate
    // detection fell back to each row's own path as its identity, two identical
    // "Discord" shortcuts could never group, and the feature reported ZERO
    // duplicates — while the list plainly showed two identical rows.
    //
    // Tests that injected pre-built AppEntry fixtures could not catch this: the
    // field was already set on them. This drives the real builder instead.
    FileIndex index(m_indexPath);
    FileIndex::WalkOutcome outcome;
    const QString root = QStringLiteral("C:\\root");
    const QString exe = QStringLiteral("C:\\root\\app\\Discord.exe");
    for (const QString &p : { QStringLiteral("C:\\root\\Discord Inc\\Discord.lnk"),
                              QStringLiteral("C:\\root\\Discord.lnk"),
                              QStringLiteral("C:\\root\\Wow.exe") }) {
        // The two shortcuts resolve to one program; Wow.exe is its own thing.
        const bool isLnk = p.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive);
        outcome.added.append(
            FileIndex::IndexEntry{p, p.toCaseFolded(), false, isLnk ? exe : p});
    }
    index.apply(outcome);

    FileProvider provider;
    provider.setIndex(&index);
    provider.setAddedSource([] { return QVector<AppEntry>{}; });

    const auto entries = provider.query(QString(), 50, false);
    QCOMPARE(entries.size(), 3);
    int withTarget = 0;
    for (const ScoredEntry &se : entries)
        if (!se.entry.launchTarget.isEmpty())
            ++withTarget;
    QCOMPARE(withTarget, 3);   // Wow.exe is its own identity, but still carries it

    // ...and end to end: the model must now see a duplicate group.
    ResultsModel m;
    QVector<AppEntry> rows;
    rows.reserve(entries.size());
    for (const ScoredEntry &se : entries)
        rows.append(se.entry);
    m.setEntries(rows);
    QCOMPARE(m.rowCount({}), 3);
    QCOMPARE(m.duplicateGroupCount(), 1);
    QCOMPARE(m.duplicateGroups().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Discord"));
}
void TstIndex::duplicateSurvivesTabSwitch_20261008()
{
    // 2026-10-08 (user: "I still don't see the duplicate button"): a TAB SWITCH
    // must not leave the duplicate cache stale.
    //
    // The cache was refreshed only by an explicit call appended to each order
    // mutator — but setFavoritesOnly takes an early `return` on the provider
    // path, so that call was dead code there. In the running app the cache kept
    // whatever it held from an early moment (measured: a recompute over 8
    // favourite rows) and the footer chip reported 0 duplicates while the All
    // tab showed 94 rows. Recomputation is now driven by the model's own
    // modelReset/rowsInserted/rowsRemoved/layoutChanged signals.
    FileIndex index(m_indexPath);
    FileIndex::WalkOutcome outcome;
    const QString root = QStringLiteral("C:\\root");
    const QString exe = QStringLiteral("C:\\root\\app\\Ollama.exe");
    for (const QString &p : { QStringLiteral("C:\\root\\Ollama\\Ollama.lnk"),
                              QStringLiteral("C:\\root\\Ollama.lnk") }) {
        outcome.added.append(FileIndex::IndexEntry{p, p.toCaseFolded(), false, exe});
    }
    index.apply(outcome);

    FileProvider fileProvider;
    fileProvider.setIndex(&index);
    fileProvider.setAddedSource([] { return QVector<AppEntry>{}; });
    AppProvider appProvider;
    CalculatorProvider calcProvider;
    CommandProvider cmdProvider;

    ResultsModel m;
    m.setProviders({ &appProvider, &fileProvider, &calcProvider, &cmdProvider });
    m.setPool(QThreadPool::globalInstance());

    const auto drain = [] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 600)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };

    m.setQuery(QStringLiteral("o"));
    drain();
    QVERIFY(m.rowCount({}) > 0);
    QCOMPARE(m.duplicateGroupCount(), 1);

    // The tab switch: favourites ON (empty here, so no duplicates) then back to
    // All. Without the signal-driven recompute the count would stay 0 and the
    // footer chip would never reappear.
    m.setFavoritesOnly(true);
    drain();
    m.setFavoritesOnly(false);
    drain();
    QVERIFY2(m.duplicateGroupCount() == 1,
             "the duplicate cache went stale across a tab switch — the footer "
             "chip depends on this signal-driven recompute");
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
