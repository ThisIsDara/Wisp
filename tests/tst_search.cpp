#include <QtTest>

#include "win/WinSearchQuery.h"

// Pure-helper contract of the Windows Search firewall (04-01): the status
// mapping (RESEARCH §2 — all 7 CatalogStatus values + catalog unavailable),
// the D-09 .exe/folder post-filter predicate, and the locked WHERE
// restriction string. The live OLE DB row walk needs a real index and is
// dev-machine manual verification (04-VALIDATION.md), never a unit test.

namespace {

// Hoisted named consts — no braced-init-lists inside QCOMPARE (MSVC rule).
const auto kOk = WinSearchQuery::IndexerState::Ok;
const auto kDisabled = WinSearchQuery::IndexerState::Disabled;
const auto kBuilding = WinSearchQuery::IndexerState::Building;
const auto kUnavailable = WinSearchQuery::IndexerState::Unavailable;

} // namespace

class TstSearch : public QObject
{
    Q_OBJECT

private slots:
    void classifyCatalogStatus();
    void isAllowedResult();
    void buildWhereRestriction();
};

void TstSearch::classifyCatalogStatus()
{
    // RESEARCH §2 locked mapping (D-17):
    // catalog unavailable (GetCatalog/CoCreateInstance failed) → Disabled
    QCOMPARE(WinSearchQuery::classifyCatalogStatus(0, false), kDisabled);

    // SHUTTING_DOWN → Unavailable (cannot be queried)
    QCOMPARE(WinSearchQuery::classifyCatalogStatus(6, true), kUnavailable);

    // crawl states → Building
    QCOMPARE(WinSearchQuery::classifyCatalogStatus(2, true), kBuilding); // RECOVERING
    QCOMPARE(WinSearchQuery::classifyCatalogStatus(3, true), kBuilding); // FULL_CRAWL
    QCOMPARE(WinSearchQuery::classifyCatalogStatus(4, true), kBuilding); // INCREMENTAL_CRAWL
    QCOMPARE(WinSearchQuery::classifyCatalogStatus(5, true), kBuilding); // PROCESSING_NOTIFICATIONS

    // IDLE / PAUSED → Ok (PAUSED still answers queries — NOT a trouble state)
    QCOMPARE(WinSearchQuery::classifyCatalogStatus(0, true), kOk);
    QCOMPARE(WinSearchQuery::classifyCatalogStatus(1, true), kOk);
}

void TstSearch::isAllowedResult()
{
    // .exe kept (D-09 post-filter gate)
    QCOMPARE(WinSearchQuery::isAllowedResult(QStringLiteral("C:\\x\\app.exe"), false), true);
    // .exe matching is case-insensitive
    QCOMPARE(WinSearchQuery::isAllowedResult(QStringLiteral("C:\\x\\APP.EXE"), false), true);
    // non-.exe file rejected (broader types deferred — CONTEXT user decision)
    QCOMPARE(WinSearchQuery::isAllowedResult(QStringLiteral("C:\\x\\file.txt"), false), false);
    // folders always kept (D-04) — even without a .exe suffix
    QCOMPARE(WinSearchQuery::isAllowedResult(QStringLiteral("C:\\x\\somefolder"), true), true);
}

void TstSearch::buildWhereRestriction()
{
    // D-09 source-level filter + PITFALLS #5 scope restriction — exact string
    // locked in the header contract (ANDed by the helper).
    const QString expected = QStringLiteral(
        "System.ItemUrl LIKE 'file:%' AND (System.FileExtension='.exe' OR System.IsFolder=TRUE)");
    QCOMPARE(WinSearchQuery::buildWhereRestriction(), expected);
}

QTEST_MAIN(TstSearch)
#include "tst_search.moc"
