#include <QtTest>

#include <QHash>

#include "core/AppEntry.h"
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
};

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
    // D-03: at most kMaxFileRows (5) file rows survive a merge; apps are never
    // dropped, regardless of where the file rows would rank.
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

    // All entries tie on score (same "file" prefix tier) → the 5 highest file
    // rows survive in alpha order, then the apps — nothing dropped from the app side.
    QCOMPARE(m.rowCount({}), 7);
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("File1.exe"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("File2.exe"));
    QCOMPARE(displayNameAt(m, 2), QStringLiteral("File3.exe"));
    QCOMPARE(displayNameAt(m, 3), QStringLiteral("File4.exe"));
    QCOMPARE(displayNameAt(m, 4), QStringLiteral("File5.exe"));
    QCOMPARE(displayNameAt(m, 5), QStringLiteral("FileExplorer"));
    QCOMPARE(displayNameAt(m, 6), QStringLiteral("FileZilla"));
}

void TstModel::pathOnlyBaseScore_D07()
{
    // D-07: a file whose NAME doesn't match the query still ranks (users type
    // "tax 2025" meaning a path) — but at the base tier (kPathMatchScore=100),
    // below every name-matched row.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Tax 2025 Planner"), {}) });
    m.setQuery(QStringLiteral("tax 2025"));
    m.setFileResults(1, QStringLiteral("tax 2025"),
                     { fileEntry(QStringLiteral("report.txt"), QStringLiteral("C:\\reports\\report.txt")) });

    const int appScore = FuzzyMatcher::score(QStringLiteral("tax 2025"), QStringLiteral("Tax 2025 Planner")).score;
    const int fileNameScore = FuzzyMatcher::score(QStringLiteral("tax 2025"), QStringLiteral("report.txt")).score;
    QCOMPARE(fileNameScore, 0); // path-only: the name itself does not match
    QVERIFY(appScore > 0);

    QCOMPARE(m.rowCount({}), 2); // the name-matched app ranks above the path-only file row
    QCOMPARE(displayNameAt(m, 0), QStringLiteral("Tax 2025 Planner"));
    QCOMPARE(displayNameAt(m, 1), QStringLiteral("report.txt"));
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
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Calculator"), {}) });
    m.setQuery(QStringLiteral("cal"));
    m.setFileResults(1, QStringLiteral("cal"),
                     { fileEntry(QStringLiteral("Calc.exe"), QStringLiteral("C:\\x\\Calc.exe")),
                       fileEntry(QStringLiteral("Report"), QStringLiteral("C:\\reports\\Report")) }); // path-only row

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
    // CUR-04 escape-hatch guard: File rows can never be curated —
    // hideSelected is a no-op and the store is never called.
    ResultsModel m;
    m.setEntries({ lnkEntry(QStringLiteral("Alpha"), QStringLiteral("C:\\apps\\a.exe")),
                   fileEntry(QStringLiteral("noise.exe"), QStringLiteral("C:\\apps\\Noise.exe")) });
    m.setQuery(QStringLiteral("noise")); // D-14: file rows render only on a live query
    QCOMPARE(m.rowCount({}), 1); // the File row is the only result
    m.selectIndex(0);

    StoreSpy spy;
    m.setHideStore([&spy](const QString &id, bool hidden) { spy.calls.append({ id, hidden }); });

    m.hideSelected();

    QCOMPARE(m.rowCount({}), 1); // unchanged
    QCOMPARE(spy.calls.isEmpty(), true); // the store never saw a File row
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