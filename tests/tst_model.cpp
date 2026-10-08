#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QSet>
#include <QThreadPool>

#include "core/AppEntry.h"
#include "core/AppProvider.h"
#include "core/CalculatorProvider.h"
#include "core/CommandProvider.h"
#include "core/FileProvider.h"
#include "core/FuzzyMatcher.h"
#include "core/ResultsModel.h"

// ResultsModel contract (D-01..D-03, D-05, D-12 seed, LAUN-05): empty-query
// full alphabetical list with first row selected, FuzzyMatcher-ranked queries
// with cached match ranges, clamped keyboard selection, and the value-copy
// snapshot API that 03-04's launch-freeze consumes.

namespace {

AppEntry lnkEntry(const QString &name, const QString &targetPath,
                  const QString &iconRef = {})
{
    AppEntry e;
    e.source = AppEntry::Source::Lnk;
    e.displayName = name;
    e.targetPath = targetPath;
    e.iconRef = iconRef; // 05-04: GetIconLocation 'path;index' output when set
    return e;
}

AppEntry uwpEntry(const QString &name, const QString &iconRef = {})
{
    AppEntry e;
    e.source = AppEntry::Source::Uwp;
    e.displayName = name;
    e.aumid = QStringLiteral("SomeFamily!SomeAppId");
    e.iconRef = iconRef; // 05-04: 'uwp:PFN|appId' when the enumerator emitted one
    return e;
}

// 2026-10-08: an index row as PRODUCTION builds it. launchTarget is what
// WinStartMenuEnumerator::resolveLnkTarget returned during the index walk — set
// for .lnk rows, empty for a plain executable (which is its own identity).
AppEntry idxEntry(const QString &name, const QString &path,
                  const QString &launchTarget = {})
{
    AppEntry e;
    e.source = AppEntry::Source::File;
    e.displayName = name;
    e.targetPath = path;
    e.launchTarget = launchTarget;
    return e;
}

// Phase-4 fixture (04-04): file rows arrive generation-stamped via
// setFileResults. displayName = filename, targetPath = full path (D-02
// subtitle), isFolder = D-04 folder glyph.
AppEntry fileEntry(const QString &name, const QString &path, bool isFolder = false)
{
    AppEntry e;
    e.source = AppEntry::Source::File;
    e.displayName = name;
    e.targetPath = path;
    e.isFolder = isFolder;
    return e;
}

QString displayNameAt(ResultsModel &m, int row)
{
    return m.data(m.index(row), ResultsModel::DisplayNameRole).toString();
}

// 05.1: HideStore spy — records (id, hidden) pairs.
struct StoreSpy { QList<QPair<QString, bool>> calls; };

} // namespace

class TstModel : public QObject
{
    Q_OBJECT

private slots:
    void emptyQueryFullList_D01_D02();
    void queryRankingWithRanges();
    void filterRoundTrip();
    void selectionBounds_LAUN05();
    void snapshotFreeze_D12();
    void alphaTieBreak_D05();
    void subtitleRole();
    void matchRangesShapeForQml();
    void qmlContracts_LAUN05();
    void iconKeyRole(); // 05-04: iconKey per parseKey grammar for Lnk/File/Uwp rows

    // Phase-4 file-results merge (04-04): D-01..D-07, D-14, D-15.
    void fileResultsMerge_D01();
    void fileCap5_D03();
    void pathOnlyBaseScore_D07();
    void staleGenerationDropped_D15();
    void subtitleFullPath_D02();
    void folderRowsIsFolderRole_D04();
    void emptyQueryAppsPlusAdded_D14();
    void selectionPreservedOnMerge();
    void fileRangesShapeForQml();

    // 07-02 D-03 path-set dedupe: scanned rows duplicating a path-bearing
    // app row are suppressed (catalog wins); UWP rows (empty path) never
    // suppress; case-folded comparison; path-based, not name-based.
    void scanRowSuppressedWhenCatalogHasSamePath_D03();
    void suppressionIsCaseFolded_D03();
    void uwpRowNeverSuppressesScanRow_D03();
    void distinctPathsBothRender_D03();
    void trackedStyleRowSuppressesScanRow_D03();

    // WR-03: relevance — results computed for OLD query text are dropped even
    // when their generation is current; the model-side guard stays monotonic
    // across catalog refreshes.
    void staleTextDropped_WR03();
    void generationGuardMonotonicAcrossRefresh_WR03();

    // 05.1 curation surface (CUR-02/CUR-03/CUR-04): hide/unhide/show-hidden
    // with query-preserving live marking (Pitfall 3) and the File-row guard.
    void showHiddenToggleReveals_CUR03();
    void unhideWritesShownIds_CUR03();
    void hideSelectedNoOpOnFile_CUR04();
    void hideAddedRow_D14(); // 2026-08-12: manual picks are hideable like apps
    void hidePreservesQuery_Pitfall3();
    void hideMarksNotRemoves_CUR02(); // 05.1 checkpoint: mark, don't delete
    void hiddenCountCountsAllHidden_CUR02();
    void hideMarksAllSameIdRows_M01();   // 05.1 review: mark ALL same-id rows
    void unhideNoOpOnVisibleRow_L01();   // 05.1 review: no spurious shown override
    void isHideableRole_CUR04();         // 2026-08-15: remove-button visibility parity
    void canRevealRole();                 // 0.1.8: "Open file location" menu-item visibility
    void favoriteKeepsQueryResults_20260915(); // provider-mode mutations keep the query view
    void tabSwitchRequeriesFavorites_20260915(); // tab switch re-runs the query in the new scope
    void duplicateDetection_20261008();       // duplicate grouping + badge semantics
    void duplicateRemoveProductionRowSpace_20261008(); // Remove works on the real row space
    void removalSurvivesRequery_20261008();       // removal persists across re-query + rescan
    void hideUnhideWorkOnProviderRows_20261008();
    void removalIsNotHiding_20261008();         // removed != hidden (no Show-hidden entry)
};

// 2026-09-15 (user report: in the All tab, typing a query and favoriting an
// app made the ENTIRE result list go empty).
//
// Production resolves rows through the Phase-10 provider fan-out, so this test
// wires providers the way main.cpp does. favoriteSelected() used to rebuild
// the view with the legacy buildAppOrder()+mergeFiles() pair, which resolves
// rows against m_entries — the WRONG row space once the visible rows come from
// the providers. The scored rows the user was looking at therefore vanished on
// every favorite/hide/unfavorite.
void TstModel::favoriteKeepsQueryResults_20260915()
{
    AppProvider appProvider;
    FileProvider fileProvider;
    CalculatorProvider calcProvider;
    CommandProvider cmdProvider;

    ResultsModel m;
    m.setProviders({ &appProvider, &fileProvider, &calcProvider, &cmdProvider });
    // main.cpp sets the global pool. It matters: with NO pool, setQuery takes
    // the legacy synchronous branch (`if (!m_pool)`) and never consults the
    // providers at all — so without this the test would exercise the wrong path
    // and see zero rows for reasons that have nothing to do with the bug.
    m.setPool(QThreadPool::globalInstance());

    // The provider fan-out is ASYNC (QtConcurrent + QFutureWatcher) whenever a
    // pool is available, and its completion lambda dereferences both the model
    // and the providers. They are function-local here, so every in-flight
    // dispatch must land before this function returns, or the watcher fires
    // against destroyed objects (observed: a QString assert at teardown,
    // reported as UnknownTestFunc).
    //
    // We cannot wait on modelReset for that: applyProviderResult's no-op guard
    // deliberately emits NOTHING when the rows are unchanged — which is exactly
    // the behaviour under test (favoriting must leave the list alone). So drain
    // the event loop for a bounded time instead; the watcher delivers finished()
    // through it.
    const auto drain = [] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 250)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    // The FIRST query always changes the row set, so wait for rows to appear.
    const auto settleFirst = [&] {
        QElapsedTimer t;
        t.start();
        while (m.rowCount({}) == 0 && t.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };

    // PRODUCTION SHAPE (main.cpp, Phase-7 pivot 07-06): the model is NEVER given
    // setEntries — m_entries stays empty and EVERY visible row comes from the
    // provider fan-out. That is precisely why the old legacy rebuild emptied the
    // list: buildAppOrder() re-derived the order from the empty m_entries while
    // the scored rows lived in the providers' results. Feeding the model a
    // catalog here (as an earlier draft of this test did) would MASK the bug,
    // so don't.
    QVector<AppEntry> catalog;
    for (const QString &n : { QStringLiteral("Alpha"), QStringLiteral("Beta"),
                              QStringLiteral("Gamma"), QStringLiteral("Delta") })
        catalog.append(lnkEntry(n, QStringLiteral("C:/apps/%1.exe").arg(n)));

    QVector<QString> lowers;
    QVector<QVector<char>> bounds;
    for (const AppEntry &e : catalog) {
        QString lower;
        QVector<char> b;
        FuzzyMatcher::buildCaches(e.displayName, &lower, &b);
        lowers.append(lower);
        bounds.append(b);
    }
    appProvider.setEntries(catalog, lowers, bounds);
    appProvider.setMeta({}, /*showHidden*/ false, /*favOnly*/ false, {});

    m.setQuery(QStringLiteral("a"));
    settleFirst();
    const int before = m.rowCount({});
    QVERIFY2(before > 0, "the typed query must return rows before favoriting");

    m.selectIndex(0);
    // The star is bound to model.isFavorite. In provider mode the rows do NOT
    // change when you favorite, so applyProviderResult's no-op guard emits
    // nothing — the model must emit the role change itself or the star silently
    // never repaints (the "star stopped working" half of the report).
    QSignalSpy favChanged(&m, &ResultsModel::dataChanged);
    m.favoriteSelected();
    drain();

    QVERIFY2(favChanged.count() > 0,
             "favoriting must emit dataChanged so the star repaints");
    bool sawFavoriteRole = false;
    for (const QList<QVariant> &c : favChanged) {
        const auto roles = c.at(2).toList();
        for (const QVariant &r : roles)
            if (r.toInt() == ResultsModel::IsFavoriteRole)
                sawFavoriteRole = true;
    }
    QVERIFY2(sawFavoriteRole, "the emitted change must carry the IsFavoriteRole");
    QCOMPARE(m.data(m.index(0), ResultsModel::IsFavoriteRole).toBool(), true);

    QCOMPARE(m.query(), QStringLiteral("a"));
    QVERIFY2(m.rowCount({}) == before,
             qPrintable(QStringLiteral("favoriting must not change the result count (was %1, now %2)")
                            .arg(before).arg(m.rowCount({}))));
    QCOMPARE(m.favoriteCount(), 1);

    m.selectIndex(0);
    m.unfavoriteSelected();
    drain();
    QCOMPARE(m.query(), QStringLiteral("a"));
    QVERIFY2(m.rowCount({}) == before,
             "unfavoriting must not change the result count either");
    QCOMPARE(m.favoriteCount(), 0);

    // hideSelected had the same defect and shares the fix — hiding from a typed
    // query must not empty the results either.
    m.selectIndex(0);
    m.hideSelected();
    drain();
    QCOMPARE(m.query(), QStringLiteral("a"));
    QVERIFY2(m.rowCount({}) == before,
             qPrintable(QStringLiteral("hiding must not change the result count (was %1, now %2)")
                            .arg(before).arg(m.rowCount({}))));
}

void TstModel::tabSwitchRequeriesFavorites_20260915()
{
    // 2026-09-15 (user report: searching in the All list, then switching to
    // Favorites, showed "No results found" even for a favorited match).
    //
    // setFavoritesOnly() used to build its target view with the legacy
    // builders, which resolve against m_entries — permanently empty in
    // production — so the morph target was always empty and the tab went
    // blank, and the query was never re-evaluated in the new scope. With
    // providers wired it must re-run the fan-out instead.
    AppProvider appProvider;
    FileProvider fileProvider;
    CalculatorProvider calcProvider;
    CommandProvider cmdProvider;

    ResultsModel m;
    m.setProviders({ &appProvider, &fileProvider, &calcProvider, &cmdProvider });
    m.setPool(QThreadPool::globalInstance());

    QVector<AppEntry> catalog;
    for (const QString &n : { QStringLiteral("Alpha"), QStringLiteral("Alpine"),
                              QStringLiteral("Beta"), QStringLiteral("Gamma") })
        catalog.append(lnkEntry(n, QStringLiteral("C:/apps/%1.exe").arg(n)));

    QVector<QString> lowers;
    QVector<QVector<char>> bounds;
    for (const AppEntry &e : catalog) {
        QString lower;
        QVector<char> b;
        FuzzyMatcher::buildCaches(e.displayName, &lower, &b);
        lowers.append(lower);
        bounds.append(b);
    }
    appProvider.setEntries(catalog, lowers, bounds);
    appProvider.setMeta({}, false, false, {});

    // Favorite exactly one of the apps the query will match ("al" -> Alpha,
    // Alpine), which is the scenario that must still show a result.
    QSet<QString> favs;
    favs.insert(QStringLiteral("C:/apps/Alpha.exe"));
    m.setFavoriteIds(favs);

    const auto settleFirst = [&] {
        QElapsedTimer t;
        t.start();
        while (m.rowCount({}) == 0 && t.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    const auto drain = [] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 250)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };

    m.setQuery(QStringLiteral("al"));   // matches Alpha AND Alpine
    settleFirst();
    QVERIFY2(m.rowCount({}) >= 2, "the All tab must show both 'al' matches");
    QCOMPARE(m.query(), QStringLiteral("al"));

    // Switch to Favorites — the query must be RE-RUN in the new scope, not
    // rebuilt from nothing.
    m.setFavoritesOnly(true);
    drain();

    QCOMPARE(m.query(), QStringLiteral("al"));   // query text preserved
    QCOMPARE(m.rowCount({}), 1);                  // pruned to the one favorite
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));

    // And back: the full set returns.
    m.setFavoritesOnly(false);
    drain();
    QVERIFY2(m.rowCount({}) >= 2, "switching back to All must restore both matches");
}

// 2026-10-08: duplicate detection. Two rows are duplicates when they share a
// display name — the real-world case being an app linked twice into Start Menu
// ("Programs\Discord Inc\Discord.lnk" plus a loose "Programs\Discord.lnk").
// The rule is name-only by measurement, not by preference: requiring a matching
// containing-folder name too found ZERO groups on the real index, because a
// duplicate install is by definition in a *different* folder. See
// ResultsModel::duplicateKeyOf().
void TstModel::duplicateDetection_20261008()
{
    // 2026-10-08: two entries are duplicates when they show the SAME TITLE.
    // Every fixture below is taken from the developer's real index, where the
    // rule was measured against three candidates (see duplicateKeyOf()).
    ResultsModel m;
    const QString discordExe = QStringLiteral("C:/Users/T/AppData/Local/Discord/Discord.exe");

    m.setEntries({
        // The user's reported case: four rows all titled "Discord" - two Start
        // Menu shortcuts, the installed binary, and the app's versioned build.
        // All four are the same app listed four times, so ALL FOUR must flag.
        // The stricter target+title rule caught only three (the versioned copy
        // has no resolved target and fell into its own bucket) - that was the
        // "it still doesn't detect Discord" report.
        idxEntry(QStringLiteral("Discord"),
                 QStringLiteral("C:/Programs/Discord Inc/Discord.lnk"), discordExe),
        idxEntry(QStringLiteral("Discord"),
                 QStringLiteral("C:/Programs/Discord.lnk"), discordExe),
        idxEntry(QStringLiteral("Discord"), discordExe),
        idxEntry(QStringLiteral("Discord"),
                 QStringLiteral("C:/Users/T/AppData/Local/Discord/app-1.0.9261/Discord.exe")),

        // An autostart item for the same app. EXCLUDED regardless of its title:
        // "remove" on a Startup shortcut means "stop launching this at login",
        // which is not duplicate cleanup. Without the exclusion every
        // self-starting app would be reported as a duplicate.
        idxEntry(QStringLiteral("Discord"),
                 QStringLiteral("C:/Programs/Startup/Discord.lnk"), discordExe),

        // Different Qt SDK versions: different titles, so never grouped. This is
        // the case a resolved-TARGET-only rule got wrong (it merged all three
        // plus Command Prompt, which share one maintenance binary).
        idxEntry(QStringLiteral("Qt 5.15.2 (MSVC 2019 64-bit)"), QStringLiteral("C:/Qt/5.15.2/qt.exe")),
        idxEntry(QStringLiteral("Qt 6.11.1 (MSVC 2022 64-bit)"), QStringLiteral("C:/Qt/6.11.1/qt.exe")),

        // Two unrelated vendors each shipping an Update.exe — the documented
        // cost of a name rule. Asserted deliberately: it is the trade we chose,
        // so a future change cannot silently reintroduce it without failing
        // here and forcing a decision.
        idxEntry(QStringLiteral("Update"), QStringLiteral("C:/VendorA/Update.exe")),
        idxEntry(QStringLiteral("Update"), QStringLiteral("C:/VendorB/Update.exe")),

        // A broken shortcut with no identity.
        idxEntry(QStringLiteral("Ghost"), QStringLiteral("C:/Programs/Ghost.lnk")),
    });

    // Three groups: Discord (4), Qt (nothing - titles differ), Update (2).
    QCOMPARE(m.duplicateGroupCount(), 2);

    const QVariantList groups = m.duplicateGroups();
    QCOMPARE(groups.size(), 2);
    const QVariantMap discord = groups.at(0).toMap();
    QCOMPARE(discord.value(QStringLiteral("name")).toString(), QStringLiteral("Discord"));
    QCOMPARE(discord.value(QStringLiteral("count")).toInt(), 4);
    const QStringList dpaths = discord.value(QStringLiteral("paths")).toStringList();
    QVERIFY(dpaths.contains(QStringLiteral("C:/Programs/Discord Inc/Discord.lnk")));
    QVERIFY(dpaths.contains(discordExe));
    // The versioned build is IN - this is the row the previous rule dropped.
    QVERIFY2(dpaths.contains(
                 QStringLiteral("C:/Users/T/AppData/Local/Discord/app-1.0.9261/Discord.exe")),
             "the versioned build copy is the same app and must be flagged");

    // The autostart shortcut is NOT, and neither are the differing Qt versions.
    const QSet<QString> flagged = [&] {
        QSet<QString> out;
        for (int i = 0; i < m.rowCount({}); ++i)
            if (m.data(m.index(i), ResultsModel::IsDuplicateRole).toBool())
                out.insert(m.data(m.index(i), ResultsModel::SubtitleRole).toString());
        return out;
    }();
    QCOMPARE(flagged.size(), 6);   // 4 Discord + 2 Update
    QVERIFY(!flagged.contains(QStringLiteral("C:/Programs/Startup/Discord.lnk")));
    QVERIFY(!flagged.contains(QStringLiteral("C:/Qt/5.15.2/qt.exe")));
    QVERIFY(!flagged.contains(QStringLiteral("C:/Qt/6.11.1/qt.exe")));
    QVERIFY(!flagged.contains(QStringLiteral("C:/Programs/Ghost.lnk")));

    // Removing a copy shrinks the group; the survivors stop being duplicates.
    QSet<QString> removed;
    m.setRemovedSource([&removed] { return removed; });
    m.setRemoveStore([&removed](const QString &id, bool on) {
        if (on) removed.insert(id); else removed.remove(id);
    });
    QVERIFY(m.removePath(QStringLiteral("C:/Programs/Discord.lnk")));
    QCOMPARE(removed.size(), 1);
    QCOMPARE(m.duplicateGroupCount(), 2);
    QCOMPARE(m.duplicateGroups().first().toMap().value(QStringLiteral("count")).toInt(), 3);
}

// 2026-10-08: the duplicates panel's Remove button, tested against the REAL
// row space. Since the 07-06 pivot the default list arrives through
// setFileResults() into m_addedEntries - production never calls setEntries()
// (AppProvider::setEntries is not wired in main.cpp), so a test built on
// setEntries() would pass while the button silently did nothing in the app.
void TstModel::duplicateRemoveProductionRowSpace_20261008()
{
    ResultsModel m;
    const auto entry = [](const QString &name, const QString &path, const QString &launch = {}) {
        AppEntry e;
        e.source = AppEntry::Source::File;   // index rows, per the FileSearch snapshot
        e.displayName = name;
        e.targetPath = path;
        e.launchTarget = launch; // resolved .lnk target, as the index walk stores
        return e;
    };
    m.setFileResults(1, QString(), {
        entry(QStringLiteral("Discord"), QStringLiteral("C:/Programs/Discord Inc/Discord.lnk"), QStringLiteral("C:/Discord/Discord.exe")),
        entry(QStringLiteral("Discord"), QStringLiteral("C:/Programs/Discord.lnk"), QStringLiteral("C:/Discord/Discord.exe")),
        entry(QStringLiteral("Steam"), QStringLiteral("C:/Programs/Steam/Steam.lnk")),
    });

    QCOMPARE(m.rowCount({}), 3);
    QCOMPARE(m.duplicateGroupCount(), 1);

    // Install the curation store BEFORE removing, exactly as main.cpp does — the
    // removal has to go through it, and the rescan below has to read it back.
    QSet<QString> persisted;
    m.setHiddenSource([&persisted] { return persisted; });
    m.setHideStore([&persisted](const QString &id, bool hidden) {
        if (hidden) persisted.insert(id); else persisted.remove(id);
    });

    // Remove one copy: it must leave the list AND take the group with it.
    QVERIFY2(m.hidePath(QStringLiteral("C:/Programs/Discord.lnk")),
             "hidePath must act on the m_addedEntries row space production uses");
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(m.duplicateGroupCount(), 0);
    QCOMPARE(m.hiddenCount(), 1);

    // A rescan of the scanned directory re-delivers EVERY entry from scratch -
    // this second setFileResults IS that rescan. The removed path must stay gone,
    // which is only true if the curation store is stamped onto the fresh batch.
    m.setFileResults(2, QString(), {
        entry(QStringLiteral("Discord"), QStringLiteral("C:/Programs/Discord Inc/Discord.lnk"), QStringLiteral("C:/Discord/Discord.exe")),
        entry(QStringLiteral("Discord"), QStringLiteral("C:/Programs/Discord.lnk"), QStringLiteral("C:/Discord/Discord.exe")),
        entry(QStringLiteral("Steam"), QStringLiteral("C:/Programs/Steam/Steam.lnk")),
    });
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(m.duplicateGroupCount(), 0);
    for (int i = 0; i < m.rowCount({}); ++i)
        QVERIFY2(m.data(m.index(i), ResultsModel::SubtitleRole).toString()
                     != QStringLiteral("C:/Programs/Discord.lnk"),
                 "the removed copy came back after a rescan of its directory");

    // Reversible: show-hidden mode brings the row back, and the group with it.
    m.setShowHidden(true);
    QCOMPARE(m.duplicateGroupCount(), 1);
}

// 2026-10-08 (user report: "I click remove, it just goes to hidden — I still
// see the app"): a removal must SURVIVE the next result batch.
//
// Production shape: typed queries resolve through the provider fan-out, and every
// batch rebuilds its entries from scratch. The model's in-memory `hidden` flag is
// therefore overwritten on each batch, so a removal only survives if the
// persisted curation store is re-read and stamped onto the fresh rows. It was
// not: hidePath() marked m_entries/m_addedEntries — which are permanently EMPTY
// since the Phase-7 pivot (07-06) — so the row came back on the next keystroke,
// and a rescan of its directory did the same.
void TstModel::removalSurvivesRequery_20261008()
{
    AppProvider appProvider;
    FileProvider fileProvider;
    CalculatorProvider calcProvider;
    CommandProvider cmdProvider;

    QVector<AppEntry> catalog;
    const QString keep = QStringLiteral("C:/Programs/Discord Inc/Discord.lnk");
    const QString dupe = QStringLiteral("C:/Programs/Discord.lnk");
    // Source::File, NOT Lnk: FileProvider tags EVERY index row Source::File, and
    // SubtitleRole only carries the full path for File rows (for Lnk it is just
    // the filename), so this is both the production shape and the only way to
    // tell the two copies apart in an assertion.
    catalog.append(idxEntry(QStringLiteral("Discord"), keep, QStringLiteral("C:/Discord/Discord.exe")));
    catalog.append(idxEntry(QStringLiteral("Discord"), dupe, QStringLiteral("C:/Discord/Discord.exe")));
    catalog.append(idxEntry(QStringLiteral("Spotify"), QStringLiteral("C:/Programs/Spotify/Spotify.lnk"), QStringLiteral("C:/Spotify/Spotify.exe")));

    QVector<QString> lowers;
    QVector<QVector<char>> bounds;
    for (const AppEntry &e : catalog) {
        QString lower;
        QVector<char> b;
        FuzzyMatcher::buildCaches(e.displayName, &lower, &b);
        lowers.append(lower);
        bounds.append(b);
    }
    appProvider.setEntries(catalog, lowers, bounds);
    appProvider.setMeta({}, /*showHidden*/ false, /*favOnly*/ false, {});

    // Stands in for CurationStore: persists writes and replays them on read.
    QSet<QString> persisted;
    auto *m = new ResultsModel;
    m->setProviders({ &appProvider, &fileProvider, &calcProvider, &cmdProvider });
    m->setPool(QThreadPool::globalInstance());
    m->setHideStore([&persisted](const QString &id, bool hidden) {
        if (hidden) persisted.insert(id); else persisted.remove(id);
    });
    m->setHiddenSource([&persisted] { return persisted; });

    const auto drain = [] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 250)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    const auto settleFirst = [&] {
        QElapsedTimer t;
        t.start();
        while (m->rowCount({}) == 0 && t.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    const auto pathAt = [m](int i) {
        return m->data(m->index(i), ResultsModel::SubtitleRole).toString();
    };

    m->setQuery(QStringLiteral("discord"));
    settleFirst();
    QCOMPARE(m->rowCount({}), 2);
    QCOMPARE(m->duplicateGroupCount(), 1);

    // Remove one copy. It must leave the list AND the duplicate set. The provider
    // fan-out is async, so the row leaves on the next batch rather than
    // synchronously - drain for it, same as any other provider mutation.
    QVERIFY(m->hidePath(dupe));
    QVERIFY2(persisted.contains(dupe), "the removal must be persisted, not just in memory");
    drain();
    QCOMPARE(m->rowCount({}), 1);
    QCOMPARE(m->duplicateGroupCount(), 0);

    // THE REGRESSION: re-run the query. The provider rebuilds every entry with
    // hidden=false, so without the curation re-read stamp the row comes back.
    m->setQuery(QStringLiteral("nothingmatches"));
    drain();
    m->setQuery(QStringLiteral("discord"));
    settleFirst();
    drain();
    QCOMPARE(m->rowCount({}), 1);
    QVERIFY2(m->data(m->index(0), ResultsModel::SubtitleRole).toString() != dupe,
             "the removed copy came back on re-query — curation was never re-read");
    QCOMPARE(m->duplicateGroupCount(), 0);

    // Reversible, as the panel promises: show-hidden brings it back.
    //
    // NOTE: the rescan case is covered in duplicateRemoveProductionRowSpace_
    // 20261008 instead. It cannot be driven from here: in production the empty
    // query is served by FileSearch -> setFileResults, not by setQuery("")'s
    // legacy branch, so re-delivering through setQuery would assert nothing
    // (it returns 0 rows in provider mode, which is what made the first
    // version of this assertion pass vacuously).
    m->setShowHidden(true);
    drain();
    bool found = false;
    for (int i = 0; i < m->rowCount({}); ++i)
        found = found || pathAt(i) == dupe;
    QVERIFY2(found, "show-hidden must still restore a removed copy");

    // Every in-flight dispatch must land before the providers/model die.
    drain();
    delete m;
}

// 2026-10-08: removal is NOT hiding. The user rejected "Hide" outright: a
// removed duplicate must leave the app, must not resurface under "Show hidden",
// and must not inflate hiddenCount — while still deleting nothing and still
// surviving a rescan. Each of those is a separate assertion because they are
// separate mechanisms (an own store group, an own filter that outranks
// show-hidden, and exclusion from hiddenCount).
// 2026-10-08 (user report: "the context menu options aren't working. I can't
// Hide/Unhide things"). Both directions were dead controls in production:
//
//   1. hideSelected/unhideSelected refused Source::File rows that were not
//      manual picks (the CUR-04 escape-hatch guard). Since the Phase-7 pivot
//      every visible row IS such a row, so both no-opped on the whole list
//      while the context menu still offered the items.
//   2. unhideSelected then rebuilt via the legacy buildAppOrder(), which
//      resolves against the permanently-empty m_entries — so had it run, it
//      would have wiped the list.
//
// Driven through the provider fan-out, exactly as main.cpp wires it.
void TstModel::hideUnhideWorkOnProviderRows_20261008()
{
    AppProvider appProvider;
    FileProvider fileProvider;
    CalculatorProvider calcProvider;
    CommandProvider cmdProvider;

    QVector<AppEntry> catalog = {
        lnkEntry(QStringLiteral("Steam"), QStringLiteral("C:/apps/Steam.exe")),
        lnkEntry(QStringLiteral("SteamPlus"), QStringLiteral("C:/apps/SteamPlus.exe")),
        lnkEntry(QStringLiteral("SteamTools"), QStringLiteral("C:/apps/SteamTools.exe")),
    };
    QVector<QString> lowers;
    QVector<QVector<char>> bounds;
    for (const AppEntry &e : catalog) {
        QString l;
        QVector<char> b;
        FuzzyMatcher::buildCaches(e.displayName, &l, &b);
        lowers.append(l);
        bounds.append(b);
    }
    appProvider.setEntries(catalog, lowers, bounds);
    appProvider.setMeta({}, false, false, {});

    QSet<QString> hidden;
    auto *m = new ResultsModel;
    m->setProviders({ &appProvider, &fileProvider, &calcProvider, &cmdProvider });
    m->setPool(QThreadPool::globalInstance());
    m->setHideStore([&hidden](const QString &id, bool on) {
        if (on) hidden.insert(id); else hidden.remove(id);
    });
    m->setHiddenSource([&hidden] { return hidden; });

    const auto drain = [] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    const auto settleFirst = [&] {
        QElapsedTimer t;
        t.start();
        while (m->rowCount({}) == 0 && t.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    const auto nameAt = [m](int i) {
        return m->data(m->index(i), ResultsModel::DisplayNameRole).toString();
    };
    const auto indexOf = [&](const QString &name) {
        for (int i = 0; i < m->rowCount({}); ++i)
            if (nameAt(i) == name) return i;
        return -1;
    };

    m->setQuery(QStringLiteral("steam"));
    settleFirst();
    drain();
    QCOMPARE(m->rowCount({}), 3);

    // Every row must REPORT itself hideable, or the ✕ button stays hidden and
    // the context menu is lying about it.
    for (int i = 0; i < m->rowCount({}); ++i)
        QVERIFY2(m->data(m->index(i), ResultsModel::IsHideableRole).toBool(),
                 "a row with a persistable identity must report isHideable");

    // HIDE one row: it must leave the visible list and be persisted.
    m->selectIndex(indexOf(QStringLiteral("SteamPlus")));
    m->hideSelected();
    drain();
    QCOMPARE(m->rowCount({}), 2);
    QVERIFY2(hidden.contains(QStringLiteral("C:/apps/SteamPlus.exe")),
             "hide must persist through the curation store");
    QCOMPARE(indexOf(QStringLiteral("SteamPlus")), -1);

    // The list must NOT be wiped (the legacy-rebuild bug).
    QCOMPARE(m->rowCount({}), 2);
    QVERIFY(indexOf(QStringLiteral("Steam")) >= 0);
    QVERIFY(indexOf(QStringLiteral("SteamTools")) >= 0);

    // It must survive a re-query (the curation store is re-stamped).
    m->setQuery(QStringLiteral("s"));
    drain();
    m->setQuery(QStringLiteral("steam"));
    drain();
    QCOMPARE(m->rowCount({}), 2);
    QCOMPARE(indexOf(QStringLiteral("SteamPlus")), -1);

    // SHOW-HIDDEN brings it back, and UNHIDE removes the override.
    m->setShowHidden(true);
    drain();
    const int back = indexOf(QStringLiteral("SteamPlus"));
    QVERIFY2(back >= 0, "show-hidden must reveal the hidden row");
    m->selectIndex(back);
    m->unhideSelected();
    drain();
    QVERIFY2(!hidden.contains(QStringLiteral("C:/apps/SteamPlus.exe")),
             "unhide must clear the persisted override");
    // ...and the list must survive that too.
    QCOMPARE(m->rowCount({}), 3);

    drain();
    delete m;
}

void TstModel::removalIsNotHiding_20261008()
{
    ResultsModel m;
    const auto entry = [](const QString &name, const QString &path, const QString &launch = {}) {
        AppEntry e;
        e.source = AppEntry::Source::File;
        e.displayName = name;
        e.targetPath = path;
        e.launchTarget = launch; // resolved .lnk target, as the index walk stores
        return e;
    };
    // One channel only (setFileResults / m_addedEntries — the production default-list
    // path). Mixing setEntries in would leave m_entries populated as well, so the
    // merged order would carry both sets and the counts below would be nonsense.
    QSet<QString> removed;
    m.setRemovedSource([&removed] { return removed; });
    m.setRemoveStore([&removed](const QString &id, bool on) {
        if (on) removed.insert(id); else removed.remove(id);
    });
    m.setFileResults(1, QString(), {
        entry(QStringLiteral("Discord"), QStringLiteral("C:/Programs/Discord Inc/Discord.lnk"), QStringLiteral("C:/Discord/Discord.exe")),
        entry(QStringLiteral("Discord"), QStringLiteral("C:/Programs/Discord.lnk"), QStringLiteral("C:/Discord/Discord.exe")),
        entry(QStringLiteral("Steam"), QStringLiteral("C:/Programs/Steam/Steam.lnk")),
    });
    QCOMPARE(m.rowCount({}), 3);
    QCOMPARE(m.duplicateGroupCount(), 1);

    QVERIFY(m.removePath(QStringLiteral("C:/Programs/Discord.lnk")));
    QCOMPARE(removed.size(), 1);
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(m.duplicateGroupCount(), 0);

    // The decisive difference from Hide: hiddenCount stays 0, so the footer
    // never grows a "Show hidden (1)" entry pointing at a removed row.
    QCOMPARE(m.hiddenCount(), 0);

    // And "Show hidden" cannot resurrect it.
    m.setShowHidden(true);
    QCOMPARE(m.rowCount({}), 2);
    for (int i = 0; i < m.rowCount({}); ++i)
        QVERIFY(m.data(m.index(i), ResultsModel::SubtitleRole).toString()
                    != QStringLiteral("C:/Programs/Discord.lnk"));

    // Survives a rescan that re-delivers everything.
    m.setFileResults(1, QString(), {
        entry(QStringLiteral("Discord"), QStringLiteral("C:/Programs/Discord Inc/Discord.lnk"), QStringLiteral("C:/Discord/Discord.exe")),
        entry(QStringLiteral("Discord"), QStringLiteral("C:/Programs/Discord.lnk"), QStringLiteral("C:/Discord/Discord.exe")),
        entry(QStringLiteral("Steam"), QStringLiteral("C:/Programs/Steam/Steam.lnk")),
    });
    m.setShowHidden(false);
    QCOMPARE(m.rowCount({}), 2);
    for (int i = 0; i < m.rowCount({}); ++i)
        QVERIFY(m.data(m.index(i), ResultsModel::SubtitleRole).toString()
                    != QStringLiteral("C:/Programs/Discord.lnk"));

    // An empty path is the only rejection.
    QVERIFY(!m.removePath(QString()));
}

void TstModel::emptyQueryFullList_D01_D02()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Gamma"), {}),
                   lnkEntry(QStringLiteral("alpha"), {}),
                   lnkEntry(QStringLiteral("Beta"), {}) });
    m.setQuery(QString());

    QCOMPARE(m.rowCount({}), 3);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("alpha")); // case-insensitive alphabetical
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Beta"));
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("Gamma"));
    QCOMPARE(m.selectedIndex(), 0); // D-02: first row selected by default
}

void TstModel::queryRankingWithRanges()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"), {}),
                   lnkEntry(QStringLiteral("Terminal"), {}),
                   lnkEntry(QStringLiteral("Notepad"), {}) });
    m.setQuery(QStringLiteral("cal"));

    QCOMPARE(m.rowCount({}), 1); // only Calculator matches the trio
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calculator"));
    QCOMPARE(m.selectedIndex(), 0);

    // MatchRangesRole is cached at query time and mirrors FuzzyMatcher output.
    // Shape contract (ResultsModel.h): QVariantList of two-int lists
    // [[start, length], ...] — one item per contiguous matched run.
    QVariantList expectedRanges;
    QVariantList run;
    run.append(0); // start
    run.append(3); // length
    // QVariant(run) wrap is REQUIRED: a bare append(run) hits QList's
    // append(const QList&) overload and splats the run flat into the parent
    // list ([[0,3]] becomes [0,3]) — the exact shape bug locked in the
    // ResultsModel.cpp comment.
    expectedRanges.append(QVariant(run));
    QCOMPARE(m.data(m.index(0), ResultsModel::MatchRangesRole).toList(), expectedRanges);
}

void TstModel::filterRoundTrip()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Gamma"), {}),
                   lnkEntry(QStringLiteral("alpha"), {}),
                   lnkEntry(QStringLiteral("Beta"), {}) });

    m.setQuery(QStringLiteral("zzz"));
    QCOMPARE(m.rowCount({}), 0);

    m.setQuery(QString());
    QCOMPARE(m.rowCount({}), 3);
    QCOMPARE(m.selectedIndex(), 0); // selection resets on every query change (D-02)
}

void TstModel::selectionBounds_LAUN05()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Gamma"), {}),
                   lnkEntry(QStringLiteral("alpha"), {}),
                   lnkEntry(QStringLiteral("Beta"), {}) });
    m.setQuery(QString());

    m.moveSelection(-1);          // at 0: up clamps to 0
    QCOMPARE(m.selectedIndex(), 0);
    m.moveSelection(+1);          // → 1
    QCOMPARE(m.selectedIndex(), 1);
    m.moveSelection(+10);         // big down delta clamps to last (2)
    QCOMPARE(m.selectedIndex(), 2);
    m.selectIndex(-5);            // explicit clamp to 0
    QCOMPARE(m.selectedIndex(), 0);
    m.selectIndex(99);            // explicit clamp to last
    QCOMPARE(m.selectedIndex(), 2);
    m.selectIndex(1);
    m.moveSelection(-7);          // PageUp = -kVisibleRows clamped
    QCOMPARE(m.selectedIndex(), 0);
    m.moveSelection(+7);          // PageDown = +kVisibleRows clamped
    QCOMPARE(m.selectedIndex(), 2);
}

void TstModel::snapshotFreeze_D12()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), {}),
                   lnkEntry(QStringLiteral("Beta"), {}),
                   lnkEntry(QStringLiteral("Gamma"), {}) });
    m.setQuery(QString());
    m.selectIndex(1);
    const AppEntry snap = m.snapshotSelected();
    QCOMPARE(snap.displayName, QStringLiteral("Beta"));

    // Re-query reshuffles the list and moves the selection elsewhere.
    m.setQuery(QStringLiteral("gamma"));
    QCOMPARE(m.rowCount({}), 1);
    QCOMPARE(m.selectedIndex(), 0);
    QCOMPARE(m.snapshotSelected().displayName, QStringLiteral("Gamma"));

    // The STORED snap is a value copy (D-12 freeze seed) — no pointer aliasing.
    QCOMPARE(snap.displayName, QStringLiteral("Beta"));
}

void TstModel::alphaTieBreak_D05()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"), {}),
                   lnkEntry(QStringLiteral("Calc.exe"), {}) });
    m.setQuery(QStringLiteral("cal"));

    QCOMPARE(m.rowCount({}), 2); // both match at equal tier
    // alphabetical tie-break ("calc.exe" < "calculator" — '.' (0x2E) < 'u'),
    // NOT catalog insertion order
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calc.exe"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Calculator"));
}

void TstModel::subtitleRole()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Notepad"),
                            QStringLiteral("C:\\Windows\\system32\\notepad.exe")),
                   uwpEntry(QStringLiteral("Calculator")) });
    m.setQuery(QString());

    // Canonical order is alphabetical (D-01): Calculator (Uwp) first, then Notepad (Lnk).
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calculator"));
    QCOMPARE(m.data(m.index(0), ResultsModel::SubtitleRole).toString(), QString()); // Uwp → empty
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Notepad"));
    QCOMPARE(m.data(m.index(1), ResultsModel::SubtitleRole).toString(),
             QStringLiteral("notepad.exe")); // Lnk → QFileInfo file name
}

void TstModel::matchRangesShapeForQml()
{
    // The QML delegate and Phase 5 consume MatchRangesRole as a QVariantList
    // of two-int lists [{start, length}, ...] per contiguous matched run —
    // this is the locked 03-05/Phase-5 highlight contract (ResultsModel.h).
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"), {}),
                   lnkEntry(QStringLiteral("Terminal"), {}) });
    m.setQuery(QStringLiteral("cal"));

    QCOMPARE(m.rowCount({}), 1); // only Calculator matches
    const QVariantList ranges = m.data(m.index(0), ResultsModel::MatchRangesRole).toList();

    // Exact shape contract: one outer list, items are 2-element int lists.
    QCOMPARE(ranges.size(), 1);
    const QVariantList run = ranges.at(0).toList();
    QCOMPARE(run.size(), 2);
    QCOMPARE(run.at(0).toInt(), 0); // start
    QCOMPARE(run.at(1).toInt(), 3); // length

    // Multi-run shape: "term" against "Terminal" → [[0,4]] single contiguous run
    // ("calc" vs "Calculator" is equivalent; the run-merge path is already
    // exercised in tst_matcher — here we lock the QML-consumable shape).
    ResultsModel m2;
    m2.setEntries({ lnkEntry(QStringLiteral("Terminal"), {}) });
    m2.setQuery(QStringLiteral("term"));
    const QVariantList ranges2 = m2.data(m2.index(0), ResultsModel::MatchRangesRole).toList();
    QCOMPARE(ranges2.size(), 1);
    QCOMPARE(ranges2.at(0).toList().at(0).toInt(), 0);
    QCOMPARE(ranges2.at(0).toList().at(1).toInt(), 4);
}

void TstModel::qmlContracts_LAUN05()
{
    // 03-05 QML consumption contracts:
    //  1. roleNames — ResultsRow.qml reads model.displayName / model.subtitle /
    //     model.matchRanges / model.aumid from this exact mapping.
    //  2. selectionChanged NOTIFY — MainWindow.qml binds
    //     `currentIndex: resultsModel.selectedIndex`; without the signal the
    //     binding evaluates once and the highlight never follows keyboard or
    //     hover moves.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), {}),
                   lnkEntry(QStringLiteral("Beta"), {}),
                   lnkEntry(QStringLiteral("Gamma"), {}) });
    m.setQuery(QString());

    const QHash<int, QByteArray> names = m.roleNames();
    QCOMPARE(names.value(ResultsModel::DisplayNameRole), QByteArray("displayName"));
    QCOMPARE(names.value(ResultsModel::SubtitleRole), QByteArray("subtitle"));
    QCOMPARE(names.value(ResultsModel::MatchRangesRole), QByteArray("matchRanges"));
    QCOMPARE(names.value(ResultsModel::AumidRole), QByteArray("aumid"));
    // 05.1: isHidden role — QML dims hidden rows via model.isHidden (CUR-03).
    QCOMPARE(names.value(ResultsModel::IsHiddenRole), QByteArray("isHidden"));
    // 2026-08-15: isHideable role — the remove button renders only on
    // hideable rows (CUR-04 parity).
    QCOMPARE(names.value(ResultsModel::IsHideableRole), QByteArray("isHideable"));

    QSignalSpy spy(&m, &ResultsModel::selectionChanged);
    m.moveSelection(+1);          // 0 → 1: notified
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.selectedIndex(), 1);
    m.selectIndex(1);             // same clamped value: no spurious notify
    QCOMPARE(spy.count(), 1);
    m.moveSelection(-7);          // 1 → 0 (clamped): notified
    QCOMPARE(spy.count(), 2);
    m.selectIndex(99);            // 0 → 2 (clamped): notified
    QCOMPARE(spy.count(), 3);
    m.selectIndex(2);             // unchanged again: quiet
    QCOMPARE(spy.count(), 3);

    // Query resets move the selection → binding re-sync fires.
    m.selectIndex(1);
    m.setQuery(QStringLiteral("gamma")); // 1 → 0 via reset
    QCOMPARE(m.selectedIndex(), 0);
    QCOMPARE(spy.count(), 5);
    m.setQuery(QStringLiteral("gamma")); // identical query: no-op altogether
    QCOMPARE(spy.count(), 5);
}

void TstModel::fileResultsMerge_D01()
{
    // D-01: file rows merge with app rows into ONE score-descending list —
    // no sectioning, no app-priority. The expected order is computed here via
    // FuzzyMatcher::score (the production scorer), so this locks the merge RULE
    // (score desc, then displayName asc case-folded per D-05) without
    // hard-coding ladder values.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"), {}),
                   lnkEntry(QStringLiteral("CalcPad"), {}) });
    m.setQuery(QStringLiteral("calc"));
    m.setFileResults(1, QStringLiteral("calc"),
                     { fileEntry(QStringLiteral("Calc"), QStringLiteral("C:\\x\\Calc")),
                       fileEntry(QStringLiteral("calc.exe"), QStringLiteral("C:\\x\\calc.exe")),
                       fileEntry(QStringLiteral("calculator.exe"), QStringLiteral("C:\\x\\calculator.exe")) });

    QVector<QPair<QString, int>> expected;
    const QString query = QStringLiteral("calc");
    expected.append({ QStringLiteral("Calculator"), FuzzyMatcher::score(query, QStringLiteral("Calculator")).score });
    expected.append({ QStringLiteral("CalcPad"), FuzzyMatcher::score(query, QStringLiteral("CalcPad")).score });
    expected.append({ QStringLiteral("Calc"), FuzzyMatcher::score(query, QStringLiteral("Calc")).score });
    expected.append({ QStringLiteral("calc.exe"), FuzzyMatcher::score(query, QStringLiteral("calc.exe")).score });
    expected.append({ QStringLiteral("calculator.exe"), FuzzyMatcher::score(query, QStringLiteral("calculator.exe")).score });
    std::sort(expected.begin(), expected.end(), [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
        if (a.second != b.second)
            return a.second > b.second; // D-01: score desc rules the merged list
        return a.first.toCaseFolded() < b.first.toCaseFolded(); // D-05 alpha tie-break
    });

    QCOMPARE(m.rowCount({}), expected.size());
    for (int i = 0; i < expected.size(); ++i)
        QCOMPARE(displayNameAt(m, i), expected.at(i).first);

    // The exact-tier file row ranks ABOVE every app row — files are NOT
    // sectioned below apps; the score decides.
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calc"));
}

void TstModel::fileCap5_D03()
{
    // D-03 (Phase-7 pivot 07-06): the per-query file cap no longer bites —
    // with the catalog unwired, file rows ARE the list. kMaxFileRows = 100
    // (the index pipeline caps candidates at 100 upstream), so all seven
    // file rows survive the merge; apps (here: two lnk rows) still rank by
    // score like any other row.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("FileExplorer"), {}),
                   lnkEntry(QStringLiteral("FileZilla"), {}) });
    m.setQuery(QStringLiteral("file"));
    m.setFileResults(1, QStringLiteral("file"),
                     { fileEntry(QStringLiteral("File1.exe"), QStringLiteral("C:\\x\\File1.exe")),
                       fileEntry(QStringLiteral("File2.exe"), QStringLiteral("C:\\x\\File2.exe")),
                       fileEntry(QStringLiteral("File3.exe"), QStringLiteral("C:\\x\\File3.exe")),
                       fileEntry(QStringLiteral("File4.exe"), QStringLiteral("C:\\x\\File4.exe")),
                       fileEntry(QStringLiteral("File5.exe"), QStringLiteral("C:\\x\\File5.exe")),
                       fileEntry(QStringLiteral("File6.exe"), QStringLiteral("C:\\x\\File6.exe")),
                       fileEntry(QStringLiteral("File7.exe"), QStringLiteral("C:\\x\\File7.exe")) });

    // All entries tie on score (same "file" prefix tier) → all file rows
    // survive in alpha order; apps rank by score like any other row.
    QCOMPARE(m.rowCount({}), 9);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("File1.exe"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("File2.exe"));
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("File3.exe"));
    QCOMPARE(displayNameAt(m, 3), QStringLiteral("File4.exe"));
    QCOMPARE(displayNameAt(m, 4), QStringLiteral("File5.exe"));
    QCOMPARE(displayNameAt(m, 5), QStringLiteral("File6.exe"));
    QCOMPARE(displayNameAt(m, 6), QStringLiteral("File7.exe"));
    QCOMPARE(displayNameAt(m, 7), QStringLiteral("FileExplorer"));
    QCOMPARE(displayNameAt(m, 8), QStringLiteral("FileZilla"));
}

void TstModel::pathOnlyBaseScore_D07()
{
    // D-07: a file whose NAME doesn't match the query still ranks (users type
    // "tax 2025" meaning a path) — but at the base tier (kPathMatchScore=100),
    // below every name-matched row.
    //
    // 2026-09-15: the fixture path was "C:\reports\report.txt", which contains
    // NO 'a' at all — so the query was never a subsequence of it and this test
    // only passed because of the very bug being fixed (see
    // nonMatchingNameNotAdmitted_20260915 in tst_index: searching "Discord"
    // listed "Wow"). The path now genuinely contains the query, which is what
    // D-07 actually describes: a path MATCH with a non-matching name.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Tax 2025 Planner"), {}) });
    m.setQuery(QStringLiteral("tax 2025"));
    m.setFileResults(1, QStringLiteral("tax 2025"),
                     { fileEntry(QStringLiteral("report.txt"),
                                 QStringLiteral("C:\\Users\\Trick\\Tax 2025\\report.txt")) });

    const int appScore = FuzzyMatcher::score(QStringLiteral("tax 2025"), QStringLiteral("Tax 2025 Planner")).score;
    const int fileNameScore = FuzzyMatcher::score(QStringLiteral("tax 2025"), QStringLiteral("report.txt")).score;
    QCOMPARE(fileNameScore, 0); // path-only: the name itself does not match
    QVERIFY(appScore > 0);
    // The path really does contain the query as a subsequence — otherwise this
    // test would be asserting the bug again rather than the feature. Note this
    // must be a plain subsequence scan, NOT FuzzyMatcher::score: that matcher
    // rejects '\' in the target and is only ever applied to display names. The
    // path-level rule lives in FileIndex::queryCandidates.
    const QString path = QStringLiteral("C:\\Users\\Trick\\Tax 2025\\report.txt");
    {
        const QString q = QStringLiteral("tax 2025").toCaseFolded();
        const QString hay = path.toCaseFolded();
        int qi = 0;
        for (int hi = 0; hi < hay.size() && qi < q.size(); ++hi)
            if (hay.at(hi) == q.at(qi))
                ++qi;
        QCOMPARE(qi, q.size()); // "tax 2025" IS a subsequence of the path
    }

    QCOMPARE(m.rowCount({}), 2); // the name-matched app ranks above the path-only file row
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Tax 2025 Planner"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("report.txt"));

    // 2026-09-15: the converse — a file whose path does NOT contain the query
    // is never admitted, however long its name is. Previously this row appeared
    // at the base tier and surfaced as a bogus result.
    ResultsModel m2;
    m2.setEntries({ lnkEntry(QStringLiteral("Tax 2025 Planner"), {}) });
    m2.setQuery(QStringLiteral("tax 2025"));
    m2.setFileResults(1, QStringLiteral("tax 2025"),
                      { fileEntry(QStringLiteral("Wow.exe"), QStringLiteral("C:\\apps\\Wow.exe")) });
    QCOMPARE(m2.rowCount({}), 1);
    QCOMPARE(displayNameAt(m2, 0), QStringLiteral("Tax 2025 Planner"));
}

void TstModel::staleGenerationDropped_D15()
{
    // D-15: the model drops results from any generation older than the latest
    // (defense in depth — FileSearch drops them too). A gen-1 delivery after
    // gen-2 is a no-op.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("FileManager"), {}) });
    m.setQuery(QStringLiteral("file"));
    m.setFileResults(1, QStringLiteral("file"),
                     { fileEntry(QStringLiteral("FileA.exe"), QStringLiteral("C:\\x\\FileA.exe")) });
    m.setFileResults(2, QStringLiteral("file"),
                     { fileEntry(QStringLiteral("FileB.exe"), QStringLiteral("C:\\x\\FileB.exe")) });
    m.setFileResults(1, QStringLiteral("file"),
                     { fileEntry(QStringLiteral("FileC.exe"), QStringLiteral("C:\\x\\FileC.exe")) }); // stale — dropped

    QCOMPARE(m.rowCount({}), 2); // FileB.exe + FileManager
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("FileB.exe"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("FileManager"));
}

void TstModel::subtitleFullPath_D02()
{
    // D-02: File rows subtitle = the FULL path (elided in QML), NOT the file
    // name. The fixture's displayName ("Calc.exe") deliberately differs from
    // the target file name ("App.exe") to prove the branch reads targetPath.
    // Lnk rows keep the QFileInfo file-name semantics (existing subtitleRole).
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"), QStringLiteral("C:\\Windows\\system32\\calc.exe")) });
    m.setQuery(QStringLiteral("cal"));
    m.setFileResults(1, QStringLiteral("cal"),
                     { fileEntry(QStringLiteral("Calc.exe"), QStringLiteral("C:\\x\\App.exe")) });

    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calc.exe")); // "calc.exe" < "calculator" (alpha)
    QCOMPARE(m.data(m.index(0), ResultsModel::SubtitleRole).toString(), QStringLiteral("C:\\x\\App.exe"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Calculator"));
    QCOMPARE(m.data(m.index(1), ResultsModel::SubtitleRole).toString(), QStringLiteral("calc.exe"));
}

void TstModel::folderRowsIsFolderRole_D04()
{
    // D-04: folder rows expose IsFolderRole for the QML monogram glyph; every
    // other row (plain files, apps) is false.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"), {}) });
    m.setQuery(QStringLiteral("cal"));
    m.setFileResults(1, QStringLiteral("cal"),
                     { fileEntry(QStringLiteral("Calc.exe"), QStringLiteral("C:\\x\\Calc.exe")),
                       fileEntry(QStringLiteral("CalFolder"), QStringLiteral("C:\\x\\CalFolder"), true) });

    QCOMPARE(m.rowCount({}), 3); // calc.exe, Calculator, CalFolder — pure alpha (all 805)
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calc.exe"));
    QCOMPARE(m.data(m.index(0), ResultsModel::IsFolderRole).toBool(), false);
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Calculator"));
    QCOMPARE(m.data(m.index(1), ResultsModel::IsFolderRole).toBool(), false);
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("CalFolder"));
    QCOMPARE(m.data(m.index(2), ResultsModel::IsFolderRole).toBool(), true); // D-04 folder glyph
}

void TstModel::emptyQueryAppsPlusAdded_D14()
{
    // D-14 (default list): the empty query renders the curated catalog PLUS
    // the added-only snapshot (CUR-04 manual picks), interleaved
    // alphabetically — ONE merged list, no sectioning.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Gamma"), {}),
                   lnkEntry(QStringLiteral("alpha"), {}),
                   lnkEntry(QStringLiteral("Beta"), {}) });
    m.setQuery(QString());
    m.setFileResults(1, QString(),
                     { fileEntry(QStringLiteral("Gizmo.exe"), QStringLiteral("C:\\x\\Gizmo.exe")) });
    QCOMPARE(m.rowCount({}), 4); // added pick joins the alphabetical default list
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("alpha"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Beta"));
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("Gamma"));
    QCOMPARE(displayNameAt(m, 3), QStringLiteral("Gizmo.exe"));

    // A non-empty query engages the file merge — the LIVE delivery carries
    // the tracked union (added picks included):
    m.setQuery(QStringLiteral("g"));
    m.setFileResults(2, QStringLiteral("g"),
                     { fileEntry(QStringLiteral("Gizmo.exe"), QStringLiteral("C:\\x\\Gizmo.exe")) });
    QCOMPARE(m.rowCount({}), 2); // Gamma + Gizmo.exe
    // ...and clearing the query restores the apps + added default list.
    m.setQuery(QString());
    QCOMPARE(m.rowCount({}), 4);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("alpha"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Beta"));
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("Gamma"));
    QCOMPARE(displayNameAt(m, 3), QStringLiteral("Gizmo.exe"));
}

void TstModel::selectionPreservedOnMerge()
{
    // D-02 applies to query changes only: file results arriving must NOT move
    // the cursor. The selection is only clamped when a merge shrinks the list
    // past it.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Aldebaran"), {}),
                   lnkEntry(QStringLiteral("Alpha"), {}),
                   lnkEntry(QStringLiteral("Alpaca"), {}) });
    m.setQuery(QStringLiteral("al"));
    m.selectIndex(2); // "Alpha" ("alpaca" < "alpha" alphabetically — 'a' < 'h')

    m.setFileResults(1, QStringLiteral("al"),
                     { fileEntry(QStringLiteral("AlphaTool.exe"), QStringLiteral("C:\\x\\AlphaTool.exe")),
                       fileEntry(QStringLiteral("AlphaWare.exe"), QStringLiteral("C:\\x\\AlphaWare.exe")) });
    QCOMPARE(m.rowCount({}), 5);
    QCOMPARE(m.selectedIndex(), 2); // preserved — never reset by file arrival
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("Alpha"));

    m.selectIndex(4); // "AlphaWare.exe" (last row)
    m.setFileResults(2, QStringLiteral("al"), {}); // empty file set shrinks the list back to the apps
    QCOMPARE(m.rowCount({}), 3);
    QCOMPARE(m.selectedIndex(), 2); // clamped to the new last row
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("Alpha"));
}

void TstModel::fileRangesShapeForQml()
{
    // Phase-5 highlight contract: name-matched file rows carry the same
    // [[start, length]] shape as app rows; path-only rows carry NO ranges
    // (there is nothing to highlight).
    //
    // 2026-09-15: the "path-only" row's path was "C:\reports\Report", which
    // does NOT contain "cal" — it only ever appeared because a name scoring 0
    // was admitted on path evidence it didn't have (the bug fixed alongside
    // this). The path now really contains the query, so this exercises the
    // genuine path-only case the contract describes: admitted, but with no
    // ranges to highlight.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"), {}) });
    m.setQuery(QStringLiteral("cal"));
    m.setFileResults(1, QStringLiteral("cal"),
                     { fileEntry(QStringLiteral("Calc.exe"), QStringLiteral("C:\\x\\Calc.exe")),
                       fileEntry(QStringLiteral("Report"), QStringLiteral("C:\\Users\\cal\\Report")) }); // path-only row

    QCOMPARE(m.rowCount({}), 3); // Calc.exe, Calculator, Report
    const QVariantList fileRanges = m.data(m.index(0), ResultsModel::MatchRangesRole).toList();
    const QVariantList appRanges = m.data(m.index(1), ResultsModel::MatchRangesRole).toList();
    QCOMPARE(fileRanges, appRanges); // both [[0,3]] — identical shape to app rows
    QCOMPARE(fileRanges.size(), 1);
    QCOMPARE(fileRanges.at(0).toList().size(), 2);
    QCOMPARE(fileRanges.at(0).toList().at(0).toInt(), 0); // start
    QCOMPARE(fileRanges.at(0).toList().at(1).toInt(), 3); // length
    QCOMPARE(m.data(m.index(2), ResultsModel::MatchRangesRole).toList().size(), 0);
}

void TstModel::staleTextDropped_WR03()
{
    // WR-03: the generation proves recency, not relevance. A result computed
    // for OLD query text — same generation, delivered while the current query
    // reads differently (debounce window) — must never merge its rows under
    // the current query.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("FileManager"), {}) });
    m.setQuery(QStringLiteral("file"));
    m.setFileResults(1, QStringLiteral("xyz"),
                     { fileEntry(QStringLiteral("Xyz.exe"), QStringLiteral("C:\\x\\Xyz.exe")) });

    QCOMPARE(m.rowCount({}), 1); // dropped — text "xyz" != current "file"
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("FileManager"));

    // Matching text + fresh generation merges normally.
    m.setFileResults(2, QStringLiteral("file"),
                     { fileEntry(QStringLiteral("FileA.exe"), QStringLiteral("C:\\x\\FileA.exe")) });
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("FileA.exe")); // ties with FileManager — alpha wins
}

void TstModel::generationGuardMonotonicAcrossRefresh_WR03()
{
    // WR-03: setEntries() (catalog refresh) clears the file ENTRIES but never
    // resets the generation guard — a delivery older than the pre-refresh
    // generation must stay dropped model-side (FileSearch's counter keeps
    // climbing, so the model-side guard must too).
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("FileManager"), {}) });
    m.setQuery(QStringLiteral("file"));
    m.setFileResults(1, QStringLiteral("file"),
                     { fileEntry(QStringLiteral("FileA.exe"), QStringLiteral("C:\\x\\FileA.exe")) });
    QCOMPARE(m.rowCount({}), 2);

    m.setEntries({ lnkEntry(QStringLiteral("FileManager"), {}) }); // D-08 refresh
    QCOMPARE(m.rowCount({}), 1); // query + file rows cleared

    // gen 0 (older than the pre-refresh gen 1) stays dropped after the
    // refresh — the guard did NOT reset to 0 with the entries.
    m.setFileResults(0, QString(),
                     { fileEntry(QStringLiteral("FileC.exe"), QStringLiteral("C:\\x\\FileC.exe")) });
    QCOMPARE(m.rowCount({}), 1);

    // A fresh generation for the new query text merges normally.
    m.setQuery(QStringLiteral("file"));
    m.setFileResults(2, QStringLiteral("file"),
                     { fileEntry(QStringLiteral("FileB.exe"), QStringLiteral("C:\\x\\FileB.exe")) });
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("FileB.exe")); // ties with FileManager — alpha wins
}

void TstModel::iconKeyRole()
{
    // 05-04: every row exposes iconKey for image://wispicons/{id} in the
    // parseKey grammar (WinIconExtractor.h): Lnk → the enumerator's iconRef
    // verbatim when set ('path;index'), else "path:" + path; Uwp → the
    // 'uwp:PFN|appId' iconRef (empty → "" so the QML monogram covers it).
    ResultsModel m;
    m.setEntries({ uwpEntry(QStringLiteral("Calculator"),
                            QStringLiteral("uwp:PFN_8wekyb3d8bbwe|AppId")),
                   uwpEntry(QStringLiteral("Mail"), {}), // no iconRef emitted
                   lnkEntry(QStringLiteral("Notepad"),
                            QStringLiteral("C:\\Windows\\system32\\notepad.exe"),
                            QStringLiteral("C:\\Windows\\system32\\notepad.exe;0")),
                   lnkEntry(QStringLiteral("Zoom"),
                            QStringLiteral("C:\\x\\Zoom.exe")) }); // no iconRef branch
    m.setQuery(QString());

    // roleNames contract — the QML delegate reads model.iconKey (05-05).
    const QHash<int, QByteArray> names = m.roleNames();
    QCOMPARE(names.value(ResultsModel::IconKeyRole), QByteArray("iconKey"));

    // Alphabetical (case-insensitive): Calculator, Mail, Notepad, Zoom.
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calculator"));
    QCOMPARE(m.data(m.index(0), ResultsModel::IconKeyRole).toString(),
             QStringLiteral("uwp:PFN_8wekyb3d8bbwe|AppId")); // Uwp → iconRef
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Mail"));
    QCOMPARE(m.data(m.index(1), ResultsModel::IconKeyRole).toString(),
             QString()); // Uwp without iconRef → "" (monogram fallback)
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("Notepad"));
    QCOMPARE(m.data(m.index(2), ResultsModel::IconKeyRole).toString(),
             QStringLiteral("C:\\Windows\\system32\\notepad.exe;0")); // Lnk → iconRef verbatim
    QCOMPARE(displayNameAt(m, 3), QStringLiteral("Zoom"));
    QCOMPARE(m.data(m.index(3), ResultsModel::IconKeyRole).toString(),
             QStringLiteral("path:C:\\x\\Zoom.exe")); // Lnk without iconRef → "path:" + path

    // File rows (D-14): rendered only under a live query; key = "path:" + path.
    ResultsModel mf;
    mf.setEntries({});
    mf.setQuery(QStringLiteral("report"));
    mf.setFileResults(1, QStringLiteral("report"),
                      { fileEntry(QStringLiteral("report.txt"),
                                  QStringLiteral("C:\\reports\\report.txt")) });
    QCOMPARE(mf.rowCount({}), 1);
    QCOMPARE(mf.data(mf.index(0), ResultsModel::IconKeyRole).toString(),
             QStringLiteral("path:C:\\reports\\report.txt"));
}

void TstModel::showHiddenToggleReveals_CUR03()
{
    // CUR-03: hidden entries are excluded by default; show-hidden mode reveals
    // them; IsHiddenRole reads true only for hidden rows (the QML dimming
    // contract). Entries carry e.hidden pre-marked — the real catalog flow.
    auto hiddenBeta = lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\b.exe"));
    hiddenBeta.hidden = true;
    auto hiddenGamma = lnkEntry(QStringLiteral("Gamma"), QStringLiteral("C:\\apps\\g.exe"));
    hiddenGamma.hidden = true;

    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\a.exe")),
                   hiddenBeta, hiddenGamma });
    m.setQuery(QString());

    QCOMPARE(m.rowCount({}), 1); // Alpha only — hidden rows excluded by default
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));

    m.setShowHidden(true); // reveal for Unhide
    QCOMPARE(m.rowCount({}), 3); // Alpha, Beta, Gamma
    QCOMPARE(m.data(m.index(0), ResultsModel::IsHiddenRole).toBool(), false); // Alpha
    QCOMPARE(m.data(m.index(1), ResultsModel::IsHiddenRole).toBool(), true);  // Beta
    QCOMPARE(m.data(m.index(2), ResultsModel::IsHiddenRole).toBool(), true);  // Gamma

    m.setShowHidden(false);
    QCOMPARE(m.rowCount({}), 1); // hidden again
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));
}

void TstModel::unhideWritesShownIds_CUR03()
{
    // CUR-03: unhideSelected writes the shown override (id, false) through the
    // seam and the row renders again WITHOUT a rebuild — the entry stays in
    // the snapshot, only the hidden flag flips (markCurated precedence).
    auto hiddenBeta = lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\b.exe"));
    hiddenBeta.hidden = true;

    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\a.exe")),
                   hiddenBeta });
    m.setQuery(QString());
    m.setShowHidden(true); // reveal so Beta is selectable
    QCOMPARE(m.rowCount({}), 2);

    m.selectIndex(1); // Beta
    StoreSpy spy;
    m.setHideStore([&spy](const QString &id, bool hidden) { spy.calls.append({ id, hidden }); });

    m.unhideSelected();

    QCOMPARE(spy.calls.size(), 1);
    QCOMPARE(spy.calls.at(0).first, QStringLiteral("C:\\apps\\b.exe")); // id derived internally
    QCOMPARE(spy.calls.at(0).second, false);                            // shown override

    m.setShowHidden(false); // default mode again — Beta is NOT hidden anymore
    QCOMPARE(m.rowCount({}), 2); // Alpha + Beta both render — no rebuild
    QCOMPARE(m.hiddenCount(), 0);
}

void TstModel::hideSelectedNoOpOnFile_CUR04()
{
    // RENAMED BEHAVIOUR (2026-10-08). This test used to assert the CUR-04
    // escape-hatch rule: "File rows can never be curated - hideSelected is a
    // no-op and the store is never called." That rule was removed, because since
    // the Phase-7 pivot (07-06) EVERY row the launcher shows is a Source::File
    // index row, so it made Hide a dead control across the whole list while the
    // context menu still offered it.
    //
    // It is kept (under the old name, so the slot list stays stable) and
    // inverted: a File row IS hideable now, the store IS called, and the row
    // leaves the list. The Command-row exclusion is asserted here instead,
    // because that is the guard that actually still holds - a command's id is
    // its typed text and must never be persisted as a hide identity.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:/apps/a.exe")),
                   fileEntry(QStringLiteral("noise.exe"), QStringLiteral("C:/apps/Noise.exe")) });
    m.setQuery(QStringLiteral("noise")); // D-14: file rows render only on a live query
    QCOMPARE(m.rowCount({}), 1);         // the File row is the only result
    m.selectIndex(0);

    StoreSpy spy;
    m.setHideStore([&spy](const QString &id, bool hidden) { spy.calls.append({ id, hidden }); });

    m.hideSelected();

    QCOMPARE(m.rowCount({}), 0);                    // it left the list
    QCOMPARE(spy.calls.size(), 1);                  // and the store saw it
    QCOMPARE(spy.calls.first().second, true);
    QCOMPARE(spy.calls.first().first, QStringLiteral("C:/apps/Noise.exe"));
}

void TstModel::hideAddedRow_D14()
{
    // 2026-08-12: the CUR-04 guard protects only TRANSIENT index file rows —
    // manual picks (fromAdded) are curated like apps: hide leaves the
    // default list, persists through the store, counts in the footer,
    // unhides in show-hidden mode, and survives a re-delivery whose entry
    // is re-stamped by the curation source (main.cpp AddedSource parity).
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\b.exe")) });
    m.setQuery(QString());
    // D-14: manual pick delivered on the added channel (Source::File entry,
    // fromAdded row) — renders on the empty query.
    m.setFileResults(0, QString(), { fileEntry(QStringLiteral("Alpha.exe"),
                                               QStringLiteral("C:\\x\\Alpha.exe")) });
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha.exe")); // "Alpha.exe" < "Beta"

    StoreSpy spy;
    m.setHideStore([&spy](const QString &id, bool hidden) { spy.calls.append({ id, hidden }); });

    m.selectIndex(0); // Alpha.exe — the added row
    m.hideSelected();

    QCOMPARE(spy.calls.size(), 1); // persisted like an app row
    QCOMPARE(spy.calls.at(0).first, QStringLiteral("C:\\x\\Alpha.exe"));
    QCOMPARE(spy.calls.at(0).second, true);
    QCOMPARE(m.rowCount({}), 1); // gone from the default list
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Beta"));
    QCOMPARE(m.hiddenCount(), 1); // footer-countable

    // Reveal + unhide (identical machinery to app rows).
    m.setShowHidden(true);
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(m.data(m.index(0), ResultsModel::IsHiddenRole).toBool(), true);
    m.selectIndex(0);
    m.unhideSelected();
    QCOMPARE(spy.calls.size(), 2);
    QCOMPARE(spy.calls.at(1).second, false);
    QCOMPARE(m.hiddenCount(), 0);
    m.setShowHidden(false);
    QCOMPARE(m.rowCount({}), 2);

    // Cross-delivery persistence: a fresh delivery carries the re-stamped
    // entry (curationStore.hiddenIds() in main.cpp) — the model trusts it.
    AppEntry redelivered = fileEntry(QStringLiteral("Alpha.exe"),
                                     QStringLiteral("C:\\x\\Alpha.exe"));
    redelivered.hidden = true;
    m.setFileResults(1, QString(), { redelivered });
    QCOMPARE(m.rowCount({}), 1); // stays hidden without a second user action
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Beta"));
}

void TstModel::hidePreservesQuery_Pitfall3()
{
    // Pitfall 3 (D-08): hiding must NEVER rebuild the catalog / reset the
    // query — the model hides the row live (marks it hidden) and keeps the
    // query text and ranking state intact.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Spotify"), QStringLiteral("C:\\apps\\Spotify.exe")),
                   lnkEntry(QStringLiteral("Spotlight"), QStringLiteral("C:\\apps\\Spotlight.exe")) });
    m.setQuery(QStringLiteral("spo"));
    QCOMPARE(m.rowCount({}), 2);
    m.selectIndex(0); // Spotify ("Spotif" < "Spotl" alphabetically)

    m.hideSelected();

    QCOMPARE(m.rowCount({}), 1); // Spotlight remains
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Spotlight"));
    QCOMPARE(m.query(), QStringLiteral("spo")); // THE Pitfall-3 assertion — query never resets
    QCOMPARE(m.hiddenCount(), 1); // footer-countable: the row was MARKED, not deleted
}

void TstModel::hideMarksNotRemoves_CUR02()
{
    // 05.1 checkpoint fix: hideSelected must MARK the entry hidden instead of
    // deleting it from m_entries — a deleted row made hiddenCount() read 0
    // in-session (the "Show hidden (N)" footer never rendered) and left
    // Unhide with no row to find until a catalog rebuild re-marked rule-
    // hidden entries. Contract: the row disappears from the VISIBLE order
    // only; it stays present, countably hidden, revealable, and unhideable.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\a.exe")),
                   lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\b.exe")),
                   lnkEntry(QStringLiteral("Gamma"), QStringLiteral("C:\\apps\\g.exe")) });
    m.setQuery(QString());

    StoreSpy spy;
    m.setHideStore([&spy](const QString &id, bool hidden) { spy.calls.append({ id, hidden }); });

    m.selectIndex(1); // Beta
    m.hideSelected();

    // Store write unchanged — hide persists the id.
    QCOMPARE(spy.calls.size(), 1);
    QCOMPARE(spy.calls.at(0).first, QStringLiteral("C:\\apps\\b.exe"));
    QCOMPARE(spy.calls.at(0).second, true);

    // Row gone from the visible order (buildAppOrder skip), NOT from m_entries.
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Gamma"));
    QCOMPARE(m.hiddenCount(), 1); // footer-countable in-session (the fix)

    // Reveal mode still shows the marked row, dimmed (isHidden role).
    m.setShowHidden(true);
    QCOMPARE(m.rowCount({}), 3); // entry still present — nothing was deleted
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Beta"));
    QCOMPARE(m.data(m.index(1), ResultsModel::IsHiddenRole).toBool(), true);

    // Unhide in-session: the row exists → unhideSelected flips it back.
    m.selectIndex(1); // Beta
    m.unhideSelected();
    QCOMPARE(spy.calls.size(), 2);
    QCOMPARE(spy.calls.at(1).first, QStringLiteral("C:\\apps\\b.exe"));
    QCOMPARE(spy.calls.at(1).second, false); // shown override
    QCOMPARE(m.hiddenCount(), 0);

    // Back in default mode the row renders again — no rebuild needed.
    m.setShowHidden(false);
    QCOMPARE(m.rowCount({}), 3);
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Beta"));
    QCOMPARE(m.data(m.index(1), ResultsModel::IsHiddenRole).toBool(), false);
}

void TstModel::hideMarksAllSameIdRows_M01()
{
    // 05.1 review (M-01): hideSelected persists ONE id, and a catalog
    // rebuild hides EVERY entry carrying it (per-user + all-users Start
    // Menu .lnk rows sharing one targetPath). In-session marking must
    // mirror that — mark ALL same-id rows, never just the first, so
    // hiddenCount and the visible order agree with the store.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\a.exe")),
                   lnkEntry(QStringLiteral("Steam"), QStringLiteral("C:\\apps\\steam.exe")),
                   lnkEntry(QStringLiteral("Steam - Games"), QStringLiteral("C:\\apps\\steam.exe")) });
    m.setQuery(QString());
    QCOMPARE(m.rowCount({}), 3);

    m.selectIndex(1); // "Steam" — the alphabetically first same-id row
    m.hideSelected();

    QCOMPARE(m.hiddenCount(), 2);          // BOTH same-id rows marked
    QCOMPARE(m.rowCount({}), 1);           // only Alpha remains visible
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));

    // The second twin is hidden too, not left visible (the pre-fix bug).
    m.setShowHidden(true);
    QCOMPARE(m.rowCount({}), 3);
    QCOMPARE(m.data(m.index(1), ResultsModel::IsHiddenRole).toBool(), true);
    QCOMPARE(m.data(m.index(2), ResultsModel::IsHiddenRole).toBool(), true);
}

void TstModel::unhideNoOpOnVisibleRow_L01()
{
    // 05.1 review (L-01): unhideSelected on a row that is NOT hidden must
    // not write a spurious shown-override — Ctrl+H in show-hidden mode on a
    // visible row would otherwise pin it visible forever against future rule
    // changes. No write, no count change, row untouched.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\a.exe")),
                   lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\b.exe")) });
    m.setQuery(QString());

    StoreSpy spy;
    m.setHideStore([&spy](const QString &id, bool hidden) { spy.calls.append({ id, hidden }); });

    m.selectIndex(1); // Beta — NOT hidden
    m.unhideSelected();

    QCOMPARE(spy.calls.isEmpty(), true); // the store never saw a no-op
    QCOMPARE(m.hiddenCount(), 0);

    // Contrast: an actually-hidden row still unhides (regression guard).
    m.selectIndex(0);
    m.hideSelected();
    QCOMPARE(spy.calls.size(), 1);
    m.setShowHidden(true);
    m.selectIndex(0); // Alpha (hidden now)
    m.unhideSelected();
    QCOMPARE(spy.calls.size(), 2);
    QCOMPARE(spy.calls.at(1).second, false); // shown override written
    QCOMPARE(m.hiddenCount(), 0);
}

void TstModel::isHideableRole_CUR04()
{
    // 2026-08-15, UPDATED 2026-10-08. The remove button renders only on rows
    // hideSelected() will actually hide, so the role has to track the real rule.
    //
    // It used to say "a transient index file row is NOT hideable (CUR-04 escape
    // hatch)". Since the Phase-7 pivot that class is the entire list, which made
    // the ✕ button disappear everywhere and left the context menu offering Hide
    // for rows it would refuse. The rule now matches hideSelected(): every row
    // with a persistable identity is hideable.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:/apps/a.exe")) });
    m.setQuery(QString());
    m.setFileResults(0, QString(), { fileEntry(QStringLiteral("Noise.exe"),
                                               QStringLiteral("C:/x/Noise.exe")) });
    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(m.data(m.index(0), ResultsModel::IsHideableRole).toBool(), true); // added pick
    QCOMPARE(m.data(m.index(1), ResultsModel::IsHideableRole).toBool(), true); // app row

    // The SAME File-source row on a live query used to report false here.
    // It must now report true, or the ✕ button vanishes and the context menu
    // disagrees with the model.
    m.setQuery(QStringLiteral("noise"));
    m.setFileResults(1, QStringLiteral("noise"),
                     { fileEntry(QStringLiteral("Noise.exe"),
                                 QStringLiteral("C:/x/Noise.exe")) });
    QCOMPARE(m.rowCount({}), 1);
    QCOMPARE(m.data(m.index(0), ResultsModel::IsHideableRole).toBool(), true);

    // The exclusion that still holds: a row with no identity at all is not
    // hideable, because there is nothing to persist against.
    ResultsModel m2;
    m2.setEntries({ lnkEntry(QStringLiteral("Ghost"), QString()) });
    m2.setQuery(QStringLiteral("ghost"));
    if (m2.rowCount({}) > 0)
        QCOMPARE(m2.data(m2.index(0), ResultsModel::IsHideableRole).toBool(), false);
}

void TstModel::canRevealRole()
{
    // 0.1.8: the right-click menu shows "Open file location" only on rows
    // revealSelected() can actually reveal. File rows (folders included) and
    // Lnk rows with a RESOLVED target qualify; Uwp/Calculator/Command rows
    // and unresolved links never do.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\alpha.exe")),
                   // Unresolved link — archetype of an empty-target Lnk row.
                   lnkEntry(QStringLiteral("Broken Link"), {}),
                   uwpEntry(QStringLiteral("Store App")) });
    m.setQuery(QString());
    QCOMPARE(m.rowCount({}), 3);
    // setEntries sorts alphabetically (D-01): Alpha / Broken Link / Store App.
    QCOMPARE(m.data(m.index(0), ResultsModel::CanRevealRole).toBool(), true);  // Lnk + resolved target
    QCOMPARE(m.data(m.index(1), ResultsModel::CanRevealRole).toBool(), false); // Lnk, empty target
    QCOMPARE(m.data(m.index(2), ResultsModel::CanRevealRole).toBool(), false); // UWP

    // File (D-04 folder included) rows reveal; the live-query channel proves
    // a plain indexed file row — the canReveal branch does NOT exclude the
    // transient file channel the way isHideable does.
    m.setQuery(QStringLiteral("data"));
    m.setFileResults(1, QStringLiteral("data"),
                     { fileEntry(QStringLiteral("Data.exe"), QStringLiteral("C:\\x\\Data.exe")) });
    QCOMPARE(m.rowCount({}), 1);
    QCOMPARE(m.data(m.index(0), ResultsModel::CanRevealRole).toBool(), true); // indexed file row

    m.setQuery(QStringLiteral("stuff"));
    m.setFileResults(2, QStringLiteral("stuff"),
                     { fileEntry(QStringLiteral("Stuff"), QStringLiteral("C:\\x\\Stuff"), true) });
    QCOMPARE(m.rowCount({}), 1);
    QCOMPARE(m.data(m.index(0), ResultsModel::CanRevealRole).toBool(), true); // folder row
}

void TstModel::hiddenCountCountsAllHidden_CUR02()
{
    // CUR-02: hiddenCount counts rule- AND user-hidden entries alike, and is
    // independent of the query / show-hidden state.
    auto h1 = lnkEntry(QStringLiteral("HiddenOne"), QStringLiteral("C:\\apps\\h1.exe"));
    h1.hidden = true;
    auto h2 = lnkEntry(QStringLiteral("HiddenTwo"), QStringLiteral("C:\\apps\\h2.exe"));
    h2.hidden = true;

    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Visible1"), {}),
                   lnkEntry(QStringLiteral("Visible2"), {}),
                   lnkEntry(QStringLiteral("Visible3"), {}),
                   h1, h2 });
    QCOMPARE(m.hiddenCount(), 2);

    m.setQuery(QStringLiteral("vis")); // query state does not affect the count
    QCOMPARE(m.hiddenCount(), 2);
    m.setShowHidden(true); // nor does show-hidden mode
    QCOMPARE(m.hiddenCount(), 2);
}

void TstModel::scanRowSuppressedWhenCatalogHasSamePath_D03()
{
    // D-03: the catalog row (icon, display name) wins over a scanned row
    // pointing at the same executable. The distinct display names prove
    // WHICH source rendered.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"),
                            QStringLiteral("C:\\Program Files\\Calc\\Calc.exe")) });
    m.setQuery(QStringLiteral("calc"));
    m.setFileResults(1, QStringLiteral("calc"),
                     { fileEntry(QStringLiteral("Calc.exe"),
                                 QStringLiteral("C:\\Program Files\\Calc\\Calc.exe")) });

    QCOMPARE(m.rowCount({}), 1); // the duplicate scan row is suppressed
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calculator")); // catalog row survived
}

void TstModel::suppressionIsCaseFolded_D03()
{
    // T-07-03: dedupe is case-insensitive on the normalized path — the scan
    // row's lower-case variant must still collide with the catalog key.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"),
                            QStringLiteral("C:\\Program Files\\Calc\\Calc.exe")) });
    m.setQuery(QStringLiteral("calc"));
    m.setFileResults(1, QStringLiteral("calc"),
                     { fileEntry(QStringLiteral("Calc.exe"),
                                 QStringLiteral("c:\\program files\\calc\\calc.exe")) });

    QCOMPARE(m.rowCount({}), 1);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Calculator"));
}

void TstModel::uwpRowNeverSuppressesScanRow_D03()
{
    // Pitfall 10: UWP rows carry an EMPTY targetPath — they never enter the
    // dedupe set, so a same-named scan row still renders.
    ResultsModel m;
    m.setEntries({ uwpEntry(QStringLiteral("Store App")) });
    m.setQuery(QStringLiteral("store"));
    m.setFileResults(1, QStringLiteral("store"),
                     { fileEntry(QStringLiteral("Store App.exe"),
                                 QStringLiteral("D:\\tools\\Store App.exe")) });

    QCOMPARE(m.rowCount({}), 2); // both render — no collision possible
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Store App"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Store App.exe"));
}

void TstModel::distinctPathsBothRender_D03()
{
    // Dedupe is path-based, not name-based (D-03: "same resolved path") —
    // two executables sharing a display name render twice.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("X.exe"),
                            QStringLiteral("C:\\a\\x.exe")) });
    m.setQuery(QStringLiteral("x.exe"));
    m.setFileResults(1, QStringLiteral("x.exe"),
                     { fileEntry(QStringLiteral("X.exe"),
                                 QStringLiteral("D:\\b\\x.exe")) });

    QCOMPARE(m.rowCount({}), 2);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("X.exe"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("X.exe"));
    QCOMPARE(m.data(m.index(0), ResultsModel::SubtitleRole).toString(),
             QStringLiteral("x.exe")); // the catalog (Lnk) row
    QCOMPARE(m.data(m.index(1), ResultsModel::SubtitleRole).toString(),
             QStringLiteral("D:\\b\\x.exe")); // the file row
}

void TstModel::trackedStyleRowSuppressesScanRow_D03()
{
    // D-03 covers tracked/added rows too: they flow through the app channel
    // (fromFiles=false rows carrying a targetPath) — the same set must
    // suppress a scan duplicate.
    AppEntry tracked;
    tracked.source = AppEntry::Source::File; // tracked-style: File source, app channel
    tracked.displayName = QStringLiteral("G.exe");
    tracked.targetPath = QStringLiteral("C:\\tools\\g.exe");
    ResultsModel m;
    m.setEntries({ tracked });
    m.setQuery(QStringLiteral("g.exe"));
    m.setFileResults(1, QStringLiteral("g.exe"),
                     { fileEntry(QStringLiteral("G.exe"),
                                 QStringLiteral("C:\\tools\\g.exe")) });

    QCOMPARE(m.rowCount({}), 1); // the app-channel row wins
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("G.exe"));
}

QTEST_MAIN(TstModel)
#include "tst_model.moc"
