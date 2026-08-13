---
phase: 04-file-search
plan: 04
subsystem: core-results-model
tags: [qt, qml, fuzzy-matcher, merge, ranking]

# Dependency graph
requires:
  - phase: 03-app-search-result-model-app-catalog
    provides: ResultsModel roles/selection contract, FuzzyMatcher::score ladder, AppEntry struct
  - phase: 04-file-search
    plan: 01
    provides: AppEntry Source::File + isFolder semantics, WinSearchQuery firewall
provides:
  - One interleaved app+file ranked list in ResultsModel (D-01, no sectioning)
  - 5-row file cap with apps never dropped (D-03)
  - Path-only file matches at base score 100 below every name match (D-07)
  - Full-path file subtitle (D-02) + IsFolderRole for the QML glyph (D-04)
  - Model-side generation guard (D-15) + empty-query apps-only behavior (D-14)
affects: [04-file-search plan 05 (wiring), phase 5 (match highlights, icons)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Row { entryIndex, fromFiles } indirection over two entry vectors — one display list indexing both app and file sources"
    - "File candidates rebuild fresh on every merge (never reuse prior merge rows) — stale-row leak guarded by fromFiles filter"

key-files:
  created: []
  modified:
    - src/core/ResultsModel.h
    - src/core/ResultsModel.cpp
    - tests/tst_model.cpp

key-decisions:
  - "m_order refactored from QVector<int> to QVector<Row {int entryIndex; bool fromFiles}> — data()/snapshotSelected resolve against m_entries or m_fileEntries"
  - "mergeFiles() collects app candidates by filtering prior m_order for fromFiles==false, never by copying merged rows (duplicate-row bug fix)"
  - "kPathMatchScore=100 sits below the 400 subsequence tier so path-only rows always rank under name matches"
  - "Selection never resets on file arrival; only clamped when the merged list shrinks past the cursor (D-02 unchanged)"

patterns-established:
  - "Merge rule: score desc, then displayName.toCaseFolded() asc across both sources — the D-01/D-05 tie-break lives in one comparator"

requirements-completed: [LAUN-02]

# Metrics
duration: 27min
completed: 2026-08-10
---

# Phase 4 Plan 4: File-Results Merge Summary

**ResultsModel extended into the merged-result engine: file rows (generation-stamped via setFileResults) interleave with app rows in one score-descending list with a 5-row file cap, path-only base tier, full-path subtitles, IsFolderRole — with zero regression across the nine existing model suites.**

## Performance

- **Duration:** 27 min (across two sessions; session 1 interrupted after the RED commit, session 2 completed GREEN + docs)
- **Started:** 2026-08-10T04:13:59Z
- **Completed:** 2026-08-10T04:40:01Z
- **Tasks:** 2 (1 RED + 1 GREEN, TDD)
- **Files modified:** 3 (ResultsModel.h, ResultsModel.cpp, tst_model.cpp)

## Accomplishments

- One merged list per D-01: files and apps ranked together by FuzzyMatcher score, alpha tie-break across both sources — proven by fileResultsMerge_D01 (exact-tier file ranks above prefix-tier apps; interleaved file/app ordering).
- D-03 file cap enforced structurally inside mergeFiles after the union sort — 7 matching files render as 5, apps untouched (fileCap5_D03).
- D-07 path-only tier: name-mismatched file rows score a base 100, below every name match (pathOnlyBaseScore_D07 with "tax 2025" query).
- D-15 model-side generation guard: stale generations are no-ops even if they arrive last (staleGenerationDropped_D15).
- D-02 full-path subtitle for Source::File rows (not the file name), D-04 IsFolderRole for folder rows, D-14 empty query stays apps-only, selection preserved (never reset by file arrival, clamped on shrink).
- MatchRangesRole keeps the [[start,length]] Phase-5 highlight shape for name-matched file rows; empty for path-only rows.
- TDD gate satisfied: `test(04-04)` RED commit exists before the `feat(04-04)` GREEN commit; RED was confirmed by a failing build (Row conversion errors before implementation).

## task Commits

Each task was committed atomically:

1. **task 1: Declare the merge contract + RED suites** - `34df609` (test)
2. **task 2: Implement the merge engine (GREEN)** - `5250319` (feat)

**Plan metadata:** `pending docs commit`

_Note: TDD plan — RED test commit and GREEN implementation commit are separate._

## Files Created/Modified

- `src/core/ResultsModel.h` - IsFolderRole, setFileResults(quint64, QVector<AppEntry>) with UI-thread contract, Row {entryIndex, fromFiles} struct, m_fileEntries/m_fileGeneration, kMaxFileRows=5, kPathMatchScore=100, buildAppOrder/mergeFiles helpers
- `src/core/ResultsModel.cpp` - setEntries clears file slate (D-08); setQuery empty→apps-only / non-empty→merge; buildAppOrder = verbatim 03-05 filter+rank; mergeFiles = union score-desc sort + file cap; setFileResults generation guard + clamp-only selection; data()/snapshotSelected Row resolution; SubtitleRole Source::File→full path; IsFolderRole case
- `tests/tst_model.cpp` - fileEntry() fixture helper + nine new suites (fileResultsMerge_D01, fileCap5_D03, pathOnlyBaseScore_D07, staleGenerationDropped_D15, subtitleFullPath_D02, folderRowsIsFolderRole_D04, emptyQueryAppsOnly_D14, selectionPreservedOnMerge, fileRangesShapeForQml)

## Decisions Made

- `Row { entryIndex, fromFiles }` indirection instead of two parallel order vectors — one display list, resolved at render time (matches the plan's task-1 spec exactly).
- File candidates are rebuilt fresh from m_fileEntries on every merge; app candidates are re-collected from prior m_order filtering out fromFiles rows (prevents stale file rows leaking into the next merge).
- Selection clamp guard handles the empty-list corner (m_order.size()-1 < 0) without hitting qBound's min<=max assert — mirrors the existing selectIndex guard.
- Test expectations use FuzzyMatcher::score computed in-test for the D-01 order assertion (locks the merge rule, not ladder values).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Duplicate file rows on repeated setFileResults merges**
- **Found during:** task 2 (GREEN; staleGenerationDropped_D15 RED suite)
- **Issue:** mergeFiles() collected "app candidates" from the existing m_order, but after the first merge m_order already contained file rows. On the next setFileResults, the stale file row was copied as a candidate AND resolved against the *new* file set (m_fileEntries.at(0) → same name twice) — rendered `[FileB.exe, FileB.exe, FileManager]`.
- **Fix:** skip `fromFiles == true` rows when collecting app candidates; the file loop rebuilds file candidates fresh from the current m_fileEntries.
- **Files modified:** src/core/ResultsModel.cpp
- **Verification:** staleGenerationDropped_D15 now green; full tst_model 20/20.
- **Committed in:** 5250319 (task 2 commit)

**2. [Test authoring - not a plan deviation] Two RED suite expectations had inverted alpha tie-break math**
- **Found during:** task 2 (first GREEN run: 17 passed, 3 failed)
- **Issue:** folderRowsIsFolderRole_D04 and selectionPreservedOnMerge assumed "CalFolder" < "Calculator" and "Alpha" < "Alpaca"; the real case-folded order is "calculator" < "calfolder" ('c' < 'f' at pos 3) and "alpaca" < "alpha" ('a' < 'h' at pos 3). The model rendered the correct alpha order — the expectations were wrong.
- **Fix:** corrected expected names/comments in both suites; assertions now match the model's (correct) rendering.
- **Files modified:** tests/tst_model.cpp
- **Verification:** both suites green.
- **Committed in:** 5250319 (task 2 commit)

---

**Total deviations:** 1 auto-fixed implementation bug (Rule 1); 1 test-authoring correction.
**Impact on plan:** The bug fix was required for D-15 correctness (defense in depth); the test corrections aligned expectations with the true comparator semantics. No scope creep.

## Issues Encountered

- **Parallel-worker build interference (04-02, out of scope):** during task-2 verification, `build.ps1` failed at the `tst_filesearch.exe` link (FileSearch.cpp not yet wired into CMakeLists by the parallel 04-02 worker). Verified my targets with a scoped preset build (`--target wisp_core tst_model`) — wisp_core and tst_model compiled and passed 20/20 while the parallel worker was mid-RED. After 04-02 completed (`0acdc2b`), the full `build.ps1` is clean and the full suite is 12/12. No files from 04-02 were touched.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 04-05 wiring can connect `FileSearch::resultsReady(generation, files)` → `ResultsModel::setFileResults` — the generation is carried end-to-end (D-15) and the model-side guard is already tested.
- QML ResultsRow.qml can consume `model.isFolder` for the D-04 monogram glyph and the full-path file subtitle.
- Phase 5 (match highlights) consumes the unchanged [[start,length]] MatchRangesRole shape, now also produced for name-matched file rows.

## Self-Check: PASSED

- [x] `src/core/ResultsModel.h` — QVector\<Row\> grep == 1; setFileResults/IsFolderRole/kMaxFileRows/kPathMatchScore present
- [x] `src/core/ResultsModel.cpp` — kPathMatchScore ≥ 1; kMaxFileRows ≥ 1; `generation < m_fileGeneration` == 1
- [x] `tests/tst_model.cpp` — 18 declared slots (9 existing + 9 new); RED commit 34df609 exists in git log
- [x] Commits 34df609 (test) + 5250319 (feat) present: `git log --oneline | grep 34df609/5250319`
- [x] `build.ps1 -Config dev` clean; `ctest --test-dir build/dev -R tst_model` green; full ctest 12/12 green

---
*Phase: 04-file-search*
*Completed: 2026-08-10*
