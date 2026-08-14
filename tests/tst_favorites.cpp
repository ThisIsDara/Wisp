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
    void fileRowsFavoriteable();
    void missingIdInert();
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

QTEST_MAIN(TstFavorites)
#include "tst_favorites.moc"