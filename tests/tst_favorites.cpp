#include <QtTest>

#include <QTemporaryDir>

#include "core/AppEntry.h"
#include "core/FavoritesStore.h"
#include "core/ResultsModel.h"

// Favorites contract (2026-08-15): FavoritesStore persists per-row favorites
// to the wisp INI under the ADDITIVE group "favorites" (id → 1, removed on
// unfavorite); ids are targetPath (Lnk/File/added) / aumid (UWP) — an id
// whose app/path vanished is inert. ResultsModel tracks favorites as a SET of
// ids (survives rebuilds for ANY row type — file rows too), exposes
// IsFavoriteRole, favoriteSelected/unfavoriteSelected (persist via the
// seam), and the favoritesOnly tab mode that filters the display order to
// favorite rows. Store round-trips go through a REAL temp INI
// (QTemporaryDir seam, tst_curation pattern) — nothing touches %APPDATA% in
// CI. Model seam = a spy (tst_model pattern).

namespace {

AppEntry lnkEntry(const QString &name, const QString &targetPath)
{
    AppEntry e;
    e.source = AppEntry::Source::Lnk;
    e.displayName = name;
    e.targetPath = targetPath;
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

AppEntry fileEntry(const QString &name, const QString &path)
{
    AppEntry e;
    e.source = AppEntry::Source::File;
    e.displayName = name;
    e.targetPath = path;
    return e;
}

QString displayNameAt(ResultsModel &m, int row)
{
    return m.data(m.index(row), ResultsModel::DisplayNameRole).toString();
}

// FavoriteStore spy — records (id, favorite) pairs.
struct FavoriteSpy { QList<QPair<QString, bool>> calls; };

} // namespace

class TstFavorites : public QObject
{
    Q_OBJECT

private slots:
    void favoritePersistsAcrossInstances();
    void unfavoriteRemovesKey();
    void missingGroupReturnsEmpty();
    void isFavoriteRole();
    void favoriteSelectedPersists();
    void unfavoriteSelectedRemoves();
    void favoritesOnlyFilters();
    void favoritesOverrideHidden();
    void fileRowsFavoriteable();
    void missingIdInert();
    void tabSwitchIsRowDeltaNotReset();
    void tabSwitchNonContiguousFavorites();
    void tabSwitchScatteredWholesaleResets();
};

void TstFavorites::favoritePersistsAcrossInstances()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    FavoritesStore store(iniPath);
    store.setFavorite(QStringLiteral("C:\\apps\\Steam.exe"), true);

    // NEW instance on the SAME ini path → the favorite survived to disk.
    FavoritesStore reloaded(iniPath);
    QVERIFY(reloaded.favoriteIds().contains(QStringLiteral("C:\\apps\\Steam.exe")));
}

void TstFavorites::unfavoriteRemovesKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    FavoritesStore store(iniPath);
    store.setFavorite(QStringLiteral("C:\\apps\\A.exe"), true);
    store.setFavorite(QStringLiteral("C:\\apps\\B.exe"), true);
    store.setFavorite(QStringLiteral("C:\\apps\\A.exe"), false); // unfavorite one

    QVERIFY(!store.favoriteIds().contains(QStringLiteral("C:\\apps\\A.exe")));
    QVERIFY(store.favoriteIds().contains(QStringLiteral("C:\\apps\\B.exe")));
}

void TstFavorites::missingGroupReturnsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // Fresh INI — no favorites group yet: D-16, silently empty.
    FavoritesStore store(iniPath);
    QVERIFY(store.favoriteIds().isEmpty());
}

void TstFavorites::isFavoriteRole()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\A.exe")),
                   lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\B.exe")) });

    // Nothing favorited yet → both rows render unfavorited.
    QVERIFY(!m.data(m.index(0), ResultsModel::IsFavoriteRole).toBool());
    QVERIFY(!m.data(m.index(1), ResultsModel::IsFavoriteRole).toBool());

    // Seeding the persisted set (startup) drives the star without a rebuild.
    m.setFavoriteIds({ QStringLiteral("C:\\apps\\B.exe") });
    QVERIFY(!m.data(m.index(0), ResultsModel::IsFavoriteRole).toBool());
    QVERIFY(m.data(m.index(1), ResultsModel::IsFavoriteRole).toBool());
    // favoriteCount reflects the persisted set — the startup tab default.
    QCOMPARE(m.favoriteCount(), 1);
}

void TstFavorites::favoriteSelectedPersists()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\A.exe")),
                   lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\B.exe")) });

    FavoriteSpy spy;
    m.setFavoriteStore([&spy](const QString &id, bool fav) { spy.calls.append({ id, fav }); });

    // Select Beta (row 1) and favorite it → seam write + in-model reflect.
    m.selectIndex(1);
    m.favoriteSelected();
    QCOMPARE(spy.calls.size(), 1);
    QCOMPARE(spy.calls.first().first, QStringLiteral("C:\\apps\\B.exe"));
    QVERIFY(spy.calls.first().second);
    QVERIFY(m.data(m.index(1), ResultsModel::IsFavoriteRole).toBool());
    // Unaffected row unchanged.
    QVERIFY(!m.data(m.index(0), ResultsModel::IsFavoriteRole).toBool());
}

void TstFavorites::unfavoriteSelectedRemoves()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\A.exe")),
                   lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\B.exe")) });

    FavoriteSpy spy;
    m.setFavoriteStore([&spy](const QString &id, bool fav) { spy.calls.append({ id, fav }); });
    m.setFavoriteIds({ QStringLiteral("C:\\apps\\B.exe") });

    m.selectIndex(1);
    m.unfavoriteSelected();
    QCOMPARE(spy.calls.size(), 1);
    QCOMPARE(spy.calls.first().first, QStringLiteral("C:\\apps\\B.exe"));
    QVERIFY(!spy.calls.first().second);
    QVERIFY(!m.data(m.index(1), ResultsModel::IsFavoriteRole).toBool());
}

void TstFavorites::favoritesOnlyFilters()
{
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\A.exe")),
                   lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\B.exe")) });
    m.setFavoriteIds({ QStringLiteral("C:\\apps\\B.exe") });

    // All tab → both rows.
    QCOMPARE(m.rowCount(), 2);
    QVERIFY(!m.favoritesOnly());

    // Favorites tab → only the favorited row.
    m.setFavoritesOnly(true);
    QVERIFY(m.favoritesOnly());
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Beta"));

    // Back to All → both rows return.
    m.setFavoritesOnly(false);
    QCOMPARE(m.rowCount(), 2);
}

void TstFavorites::favoritesOverrideHidden()
{
    // 2026-08-17 regression (observed live): a row that is BOTH hidden
    // (curationHidden) and favorited vanished from the Favorites tab — the
    // empty-query build skipped hidden rows BEFORE filterFavorites ran, while
    // search never skips hidden file rows, so the row showed when typed but
    // never in Favorites. Rule: in Favorites mode, favorited rows are exempt
    // from the hidden skip (the favorites set is a positive user marker).
    ResultsModel m;
    auto hiddenFav = lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\B.exe"));
    hiddenFav.hidden = true;
    auto hiddenPlain = lnkEntry(QStringLiteral("Gamma"), QStringLiteral("C:\\apps\\G.exe"));
    hiddenPlain.hidden = true;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\A.exe")),
                   hiddenFav, hiddenPlain });
    m.setFavoriteIds({ QStringLiteral("C:\\apps\\B.exe") });

    // All tab: hidden rows stay hidden (CUR-03) — Alpha only.
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));

    // Favorites tab: the hidden-but-favorited row appears; the hidden
    // non-favorite stays excluded.
    m.setFavoritesOnly(true);
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Beta"));

    // All tab: hidden skip restored (favorites mode off).
    m.setFavoritesOnly(false);
    QCOMPARE(m.rowCount(), 1);
}

void TstFavorites::fileRowsFavoriteable()
{
    // In the 07-06 pivot rows are scan/added File entries — favorites are a
    // positive marker (no curation escape-hatch exclusion), so a file row
    // favorites by its targetPath like an app.
    ResultsModel m;
    m.setEntries({ fileEntry(QStringLiteral("tool.exe"), QStringLiteral("C:\\apps\\tool.exe")) });

    FavoriteSpy spy;
    m.setFavoriteStore([&spy](const QString &id, bool fav) { spy.calls.append({ id, fav }); });

    m.favoriteSelected();
    QCOMPARE(spy.calls.size(), 1);
    QCOMPARE(spy.calls.first().first, QStringLiteral("C:\\apps\\tool.exe"));
    QVERIFY(m.data(m.index(0), ResultsModel::IsFavoriteRole).toBool());
}

void TstFavorites::missingIdInert()
{
    // An entry with NO identity (empty path AND aumid) cannot be favorited —
    // nothing to persist, no crash.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Orphan"), {}) });

    FavoriteSpy spy;
    m.setFavoriteStore([&spy](const QString &id, bool fav) { spy.calls.append({ id, fav }); });

    m.favoriteSelected();
    QVERIFY(spy.calls.isEmpty());
    QVERIFY(!m.data(m.index(0), ResultsModel::IsFavoriteRole).toBool());
}

void TstFavorites::tabSwitchIsRowDeltaNotReset()
{
    // Phase-11 perf fix (2026-09-02): switching tabs MUST NOT beginResetModel
    // (that tears down every ListView delegate — the felt stutter). It must
    // emit only the row-level insert/remove deltas so surviving delegates are
    // reused. Spy the model-change signals: a reset fires modelReset(); the
    // incremental path fires only rowsInserted/rowsRemoved (never modelReset).
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\A.exe")),
                   lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\B.exe")),
                   lnkEntry(QStringLiteral("Gamma"), QStringLiteral("C:\\apps\\G.exe")) });
    m.setFavoriteIds({ QStringLiteral("C:\\apps\\B.exe") });

    QSignalSpy resetSpy(&m, &ResultsModel::modelReset);
    QSignalSpy insertSpy(&m, &ResultsModel::rowsInserted);
    QSignalSpy removeSpy(&m, &ResultsModel::rowsRemoved);

    m.setFavoritesOnly(true);   // All → Favorites: remove Alpha, Gamma
    QCOMPARE(resetSpy.count(), 0);   // must NOT reset
    QVERIFY(removeSpy.count() > 0);  // row removals drive the switch
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Beta"));

    m.setFavoritesOnly(false);  // Favorites → All: re-insert Alpha, Gamma
    QCOMPARE(resetSpy.count(), 0);   // still no reset
    QVERIFY(insertSpy.count() > 0);
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("Gamma"));
}

void TstFavorites::tabSwitchScatteredWholesaleResets()
{
    // Phase-11 (2026-09-02): a WHOLESALE tab switch over a large scattered
    // set must fall back to a single reset — emitting hundreds of tiny
    // row-delta batches would each force a QML layout pass (thumbnail "very
    // laggy Favorites→All"). Scope: > kMaxDeltaRuns getRemoved+/Inserted runs
    // triggers ONE beginResetModel; content must still be exactly right.
    ResultsModel m;
    QVector<AppEntry> entries;
    for (int i = 0; i < 200; ++i)
        entries.append(lnkEntry(QStringLiteral("App%1").arg(i, 3, 10, QLatin1Char('0')),
                                QStringLiteral("C:\\apps\\app%1.exe").arg(i)));
    m.setEntries(entries);
    QSet<QString> favs;
    for (int i = 0; i < 200; i += 3)
        favs.insert(QStringLiteral("C:\\apps\\app%1.exe").arg(i));
    m.setFavoriteIds(favs);

    QSignalSpy resetSpy(&m, &ResultsModel::modelReset);
    QSignalSpy insertSpy(&m, &ResultsModel::rowsInserted);
    QSignalSpy removeSpy(&m, &ResultsModel::rowsRemoved);

    // All (200) → Favorites (~67, scattered): must go through ONE reset, not
    // ~67 removal batches.
    m.setFavoritesOnly(true);
    QCOMPARE(m.rowCount(), 67);
    QVERIFY(resetSpy.count() >= 1);          // wholesale → reset (few gestures)
    QVERIFY(removeSpy.count() + insertSpy.count() <= 1); // no per-row deltas
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("App000"));

    // Favorites → All (back to 200): same — one reset gesture, exact content.
    resetSpy.clear();
    m.setFavoritesOnly(false);
    QCOMPARE(m.rowCount(), 200);
    QVERIFY(resetSpy.count() >= 1);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("App000"));
    QCOMPARE(displayNameAt(m, 199), QStringLiteral("App199"));
}

void TstFavorites::tabSwitchNonContiguousFavorites()
{
    // Favorited rows that are NOT adjacent in the All ordering (Alpha and
    // Gamma favorited, Beta not) — the tab switch must remove/insert the
    // non-favorite in the middle without disturbing the survivors' order.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\A.exe")),
                   lnkEntry(QStringLiteral("Beta"), QStringLiteral("C:\\apps\\B.exe")),
                   lnkEntry(QStringLiteral("Gamma"), QStringLiteral("C:\\apps\\G.exe")),
                   lnkEntry(QStringLiteral("Delta"), QStringLiteral("C:\\apps\\D.exe")) });
    m.setFavoriteIds({ QStringLiteral("C:\\apps\\A.exe"),
                       QStringLiteral("C:\\apps\\G.exe") });

    m.setFavoritesOnly(true);
    // Favorites tab shows Alpha then Gamma (Beta + Delta removed, order kept).
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Gamma"));

    m.setFavoritesOnly(false);
    // Back to All: full alphabetical order (setEntries sorts: Alpha, Beta,
    // Delta, Gamma — Delta precedes Gamma case-insensitively).
    QCOMPARE(m.rowCount(), 4);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Alpha"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("Beta"));
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("Delta"));
    QCOMPARE(displayNameAt(m, 3), QStringLiteral("Gamma"));
}

QTEST_MAIN(TstFavorites)
#include "tst_favorites.moc"