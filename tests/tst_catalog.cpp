#include <QtTest>

#include <QThread>

#include <chrono>
#include <exception>
#include <vector>

#include "core/AppCatalog.h"
#include "core/AppEntry.h"

// AppCatalog contract (D-08/D-09/D-10): worker-thread build off the hotkey
// path, age-based refresh (10-min default), silent atomic swap, .lnk-wins
// dedupe on exact case-insensitive full-name collisions. All behaviors are
// proven with injected scanner fakes — no live Windows session needed
// (RESEARCH Validation Architecture: tst_catalog).

namespace {

AppEntry lnkEntry(const QString &name,
                  const QString &targetPath = QStringLiteral("C:\\apps\\x.exe"))
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

using Scanner = AppCatalog::Scanner;

Scanner scannerOf(QVector<AppEntry> entries)
{
    return [entries]() { return entries; }; // lvalue copy: scanner is copyable
}

Scanner slowScannerOf(int waitMs, QVector<AppEntry> entries)
{
    return [waitMs, entries]() {
        QThread::msleep(waitMs); // blocking fake — readers must never wait (D-08)
        return entries;
    };
}

Scanner throwingScanner()
{
    return []() -> QVector<AppEntry> { throw std::runtime_error("scanner boom"); };
}

// 05.1: curation-source fake — mirrors scannerOf's lvalue-copy discipline.
AppCatalog::CurationSource curationSourceOf(const AppCatalog::CurationData &data)
{
    return [data]() { return data; };
}

QStringList namesOf(const QVector<AppEntry> &entries)
{
    QStringList names;
    for (const AppEntry &e : entries)
        names << e.displayName;
    return names;
}

} // namespace

class TstCatalog : public QObject
{
    Q_OBJECT

private slots:
    void dedupePrecedence_D10();
    void alphabeticalOrder_D03();
    void ageRefresh_D08();
    void silentSwapConsistency_D08();
    void buildFailureIsolation();
    void startupIsNonBlocking();
    // 05.1 curation integration (CUR-01/CUR-02/CUR-04)
    void markingAfterDedupe_Pitfall1();
    void rebuildRemarkFromStore_CUR02();
    void uninstalledHiddenIdInert_CUR02();
    void fileRowsNeverCurated_CUR04();
};

void TstCatalog::dedupePrecedence_D10()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);

    // Scanner A = .lnk apps, scanner B = UWP apps. "calculator" collides
    // case-insensitively with the .lnk "Calculator" → UWP suppressed (D-10).
    // "CalculatorX" differs beyond exact full-name equality → kept.
    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Calculator")),
                                      lnkEntry(QStringLiteral("Terminal")) }),
                          scannerOf({ uwpEntry(QStringLiteral("calculator")),
                                      uwpEntry(QStringLiteral("CalculatorX")),
                                      uwpEntry(QStringLiteral("Photos")) }) });
    catalog.start();
    QVERIFY(refreshed.wait(2000));

    const QVector<AppEntry> entries = catalog.entries();
    QCOMPARE(entries.size(), 4);
    // Sorted alphabetically (D-03); the colliding UWP "calculator" is gone,
    // while non-colliding "Photos" (UWP-only) and case-SENSITIVE
    // "CalculatorX" remain.
    QCOMPARE(namesOf(entries),
             QStringList({ QStringLiteral("Calculator"), QStringLiteral("CalculatorX"),
                           QStringLiteral("Photos"), QStringLiteral("Terminal") }));
    QCOMPARE(entries.at(0).source, AppEntry::Source::Lnk); // .lnk entry won the collision
    QCOMPARE(entries.at(1).source, AppEntry::Source::Uwp); // case-SENSITIVE name kept
    QCOMPARE(entries.at(2).source, AppEntry::Source::Uwp); // non-colliding UWP-only app remains
    QCOMPARE(entries.at(3).source, AppEntry::Source::Lnk); // D-10 suppressions are exact-only
}

void TstCatalog::alphabeticalOrder_D03()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);

    // Scanners return scrambled order; the catalog sorts (D-03, same
    // case-insensitive comparator ResultsModel::setEntries uses).
    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("gamma")) }),
                          scannerOf({ uwpEntry(QStringLiteral("Beta")) }),
                          scannerOf({ lnkEntry(QStringLiteral("Alpha")) }) });
    catalog.start();
    QVERIFY(refreshed.wait(2000));

    QCOMPARE(namesOf(catalog.entries()),
             QStringList({ QStringLiteral("Alpha"), QStringLiteral("Beta"),
                           QStringLiteral("gamma") }));
}

void TstCatalog::ageRefresh_D08()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);
    catalog.setRefreshInterval(std::chrono::milliseconds(50)); // test hook for the 10-min default

    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Alpha")),
                                      lnkEntry(QStringLiteral("Beta")) }) });
    catalog.start();
    QVERIFY(refreshed.wait(2000));
    QCOMPARE(catalog.entries().size(), 2);

    // Age the catalog past the interval, then ask for freshness with a NEW
    // scanner set → rebuild fires and swaps in the bigger list.
    QTest::qWait(80);
    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Alpha")),
                                      lnkEntry(QStringLiteral("Beta")),
                                      lnkEntry(QStringLiteral("Gamma")) }) });
    catalog.ensureFresh();
    QVERIFY(refreshed.wait(2000));
    QCOMPARE(catalog.entries().size(), 3);

    // Negative: ensureFresh() immediately after a build does NOT rebuild
    // (age < interval) — and there is exactly one refreshed() per swap
    // (T-03-03-02 single-flight: no double emission).
    catalog.ensureFresh();
    QTest::qWait(100);
    QCOMPARE(refreshed.count(), 2);
}

void TstCatalog::silentSwapConsistency_D08()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);
    catalog.setRefreshInterval(std::chrono::milliseconds(50));

    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Alpha")),
                                      lnkEntry(QStringLiteral("Beta")) }) });
    catalog.start();
    QVERIFY(refreshed.wait(2000));
    QCOMPARE(catalog.entries().size(), 2);
    refreshed.clear();

    QTest::qWait(80); // stale
    // Second scanner blocks 200 ms — a rebuild longer than a reader's
    // patience must not hold the reader.
    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Alpha")),
                                      lnkEntry(QStringLiteral("Beta")) }),
                          slowScannerOf(200, { uwpEntry(QStringLiteral("Gamma")),
                                               uwpEntry(QStringLiteral("Delta")) }) });
    catalog.ensureFresh();

    // While the rebuild is in flight, readers still see the OLD snapshot —
    // no partial list, no blocking (silent swap, D-08 / T-03-03-01).
    const QVector<AppEntry> midSwap = catalog.entries();
    QCOMPARE(midSwap.size(), 2);
    QCOMPARE(namesOf(midSwap),
             QStringList({ QStringLiteral("Alpha"), QStringLiteral("Beta") }));

    QVERIFY(refreshed.wait(5000)); // swap completes
    QCOMPARE(catalog.entries().size(), 4);
    QCOMPARE(refreshed.count(), 1); // exactly one refreshed() per swap — no flicker
}

void TstCatalog::buildFailureIsolation()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);
    QSignalSpy failed(&catalog, &AppCatalog::buildFailed);
    catalog.setRefreshInterval(std::chrono::milliseconds(50));

    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Alpha")),
                                      lnkEntry(QStringLiteral("Beta")) }) });
    catalog.start();
    QVERIFY(refreshed.wait(2000));
    QCOMPARE(catalog.entries().size(), 2);

    QTest::qWait(80); // stale
    // One scanner throws; the healthy one still contributes (per-scanner
    // isolation, T-03-03-03). Previous catalog stays intact.
    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Alpha")),
                                      lnkEntry(QStringLiteral("Beta")) }),
                          throwingScanner() });
    catalog.ensureFresh();

    QVERIFY(failed.wait(2000));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().toInt(), 1); // errorCount == 1

    // No crash, no partial list: the previous snapshot survived the failed
    // build; the swap still completed with the surviving entries.
    QCOMPARE(catalog.entries().size(), 2);
    QCOMPARE(refreshed.count(), 2);
}

void TstCatalog::startupIsNonBlocking()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);

    // A slow first scan must not block the caller: entries() right after
    // start() is an empty-but-valid snapshot (the <100ms-show proof at unit
    // level — hotkey path never touches the scan, D-08 / PITFALLS #14).
    catalog.setScanners({ slowScannerOf(200, { lnkEntry(QStringLiteral("Alpha")),
                                               lnkEntry(QStringLiteral("Beta")) }) });
    catalog.start();

    const QVector<AppEntry> immediately = catalog.entries();
    QVERIFY(immediately.isEmpty()); // worker still sleeping — empty snapshot, no blocking

    QVERIFY(refreshed.wait(5000));
    QCOMPARE(catalog.entries().size(), 2);
}

// 05.1 Pitfall 1 (research): marking BEFORE dedupe would let a hidden .lnk's
// suppressed UWP twin resurrect. Marking runs strictly AFTER dedupeLnkOverUwp
// (plan 05.1-02), so the colliding UWP "Calculator" stays suppressed and only
// the marked .lnk survives.
void TstCatalog::markingAfterDedupe_Pitfall1()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);

    // Scanner A = .lnk Calculator (targetPath C:\apps\Calculator.exe),
    // scanner B = UWP Calculator (dedupe collision, D-10). The .lnk's path is
    // user-hidden → the surviving row must be the hidden .lnk alone.
    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Calculator"),
                                               QStringLiteral("C:\\apps\\Calculator.exe")) }),
                          scannerOf({ uwpEntry(QStringLiteral("Calculator")) }) });
    catalog.setCurationSource(curationSourceOf(
        { { QStringLiteral("C:\\apps\\Calculator.exe") }, {} })); // CurationData{hiddenIds, shownIds}
    catalog.start();
    QVERIFY(refreshed.wait(2000));

    QCOMPARE(catalog.entries().size(), 1); // UWP twin did NOT resurrect
    QCOMPARE(catalog.entries().at(0).source, AppEntry::Source::Lnk);
    QVERIFY(catalog.entries().at(0).hidden);
}

// CUR-02: rebuilds re-mark from the injected curation source — user-hidden
// entries stay hidden; a shown override beats hiddenIds AND default rules
// (explicit precedence contract). Fixture names are ON the allowlist
// ("Spotify") so a default-visible row exists independently of rules.
void TstCatalog::rebuildRemarkFromStore_CUR02()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);
    catalog.setRefreshInterval(std::chrono::milliseconds(50)); // pre-start, like ageRefresh_D08

    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Noise"), QStringLiteral("C:\\apps\\Noise.exe")),
                                      lnkEntry(QStringLiteral("Spotify"), QStringLiteral("C:\\apps\\Spotify.exe")) }) });
    catalog.setCurationSource(curationSourceOf({ { QStringLiteral("C:\\apps\\Noise.exe") }, {} }));
    catalog.start();
    QVERIFY(refreshed.wait(2000));

    // Alphabetical: Noise (user-hidden) before Spotify (allowlisted, visible).
    QVERIFY(catalog.entries().at(0).hidden);
    QVERIFY(!catalog.entries().at(1).hidden);

    // Rebuild with the SAME curation source → the user hide survives (CUR-02).
    QTest::qWait(80); // age past the 50 ms interval
    catalog.ensureFresh();
    QVERIFY(refreshed.wait(2000));
    QVERIFY(catalog.entries().at(0).hidden);
    QVERIFY(!catalog.entries().at(1).hidden);

    // Swap the source: hiddenIds AND shownIds both name Noise — the shown
    // override wins (last-action-wins precedence).
    catalog.setCurationSource(curationSourceOf({ { QStringLiteral("C:\\apps\\Noise.exe") },
                                                  { QStringLiteral("C:\\apps\\Noise.exe") } }));
    QTest::qWait(80);
    catalog.ensureFresh();
    QVERIFY(refreshed.wait(2000));
    QVERIFY(!catalog.entries().at(0).hidden); // shown override beats hiddenIds
    QVERIFY(!catalog.entries().at(1).hidden);
}

// CUR-02: an id whose app was uninstalled is inert — the next rebuild drops
// the entry with no crash (QSet::contains on a never-produced id). Fixture
// names are ON the allowlist so both rows are visible by default.
void TstCatalog::uninstalledHiddenIdInert_CUR02()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);

    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Spotify"), QStringLiteral("C:\\apps\\Spotify.exe")),
                                      lnkEntry(QStringLiteral("Steam"), QStringLiteral("C:\\apps\\Steam.exe")) }) });
    catalog.setCurationSource(curationSourceOf({ { QStringLiteral("C:\\gone\\app.exe") }, {} }));
    catalog.start();
    QVERIFY(refreshed.wait(2000));

    QCOMPARE(catalog.entries().size(), 2);
    QVERIFY(!catalog.entries().at(0).hidden);
    QVERIFY(!catalog.entries().at(1).hidden);
}

// CUR-04 escape hatch: Source::File rows are never curated — a File row whose
// path IS in hiddenIds stays visible, while a curated .lnk stays hidden.
void TstCatalog::fileRowsNeverCurated_CUR04()
{
    AppCatalog catalog;
    QSignalSpy refreshed(&catalog, &AppCatalog::refreshed);

    AppEntry fileRow;
    fileRow.source = AppEntry::Source::File;
    fileRow.displayName = QStringLiteral("noise.exe");
    fileRow.targetPath = QStringLiteral("C:\\apps\\Noise.exe");

    catalog.setScanners({ scannerOf({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\a.exe")),
                                      fileRow }) });
    catalog.setCurationSource(curationSourceOf({ { QStringLiteral("C:\\apps\\Noise.exe"),
                                                   QStringLiteral("C:\\apps\\a.exe") },
                                                  {} }));
    catalog.start();
    QVERIFY(refreshed.wait(2000));

    QCOMPARE(catalog.entries().size(), 2);
    // Sorted: "alpha" < "noise.exe" — Alpha (Lnk, hidden) first, File row last.
    QVERIFY(catalog.entries().at(0).hidden);   // Lnk curated via hiddenIds
    QCOMPARE(catalog.entries().at(1).displayName, QStringLiteral("noise.exe"));
    QVERIFY(!catalog.entries().at(1).hidden);  // File row immune (CUR-04)
}

QTEST_MAIN(TstCatalog)
#include "tst_catalog.moc"