---
phase: 03-app-search-result-model-app-catalog
plan: 01
subsystem: core
tags: [qt6, qabstractlistmodel, fuzzy-matching, qml, qtest]

# Dependency graph
requires:
  - phase: 02-global-hotkey-toggle
    provides: wisp_core static lib pattern, test wiring, controller contracts
provides:
  - AppEntry shared struct (Source enum, displayName, targetPath, arguments, aumid, iconRef) — the phase contract for 03-02/03-03/03-04/03-05
  - FuzzyMatcher::score returning score + exact match ranges (LAUN-06 Phase-5 highlight data contract)
  - ResultsModel QAbstractListModel: empty-query alphabetical list, ranked queries, clamped selection, value-copy snapshot
  - tst_matcher (8 behaviors) + tst_model (7 behaviors) green targets
affects: [03-02-app-enumeration, 03-03-app-catalog, 03-04-launch, 03-05-result-list-ui, phase-05 icons + highlight rendering]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Tier-ladder fuzzy scoring: binary threshold tiers (exact 1000 > prefix 800 > boundary 600 > subsequence 400) with bonuses capped below the 200-point tier gap — ladder invariant is absolute by construction"
    - "Per-char QChar::toCaseFolded() comparison instead of QString::toCaseFolded() — avoids multi-char fold (ß→ss) position-mapping hazards; ranges computed in original-string space"
    - "Model/render split: match ranges computed ONCE at setQuery time and cached in a QVector<Result> aligned with the row-order permutation — data() is O(1) (D-06 budget)"

key-files:
  created: [src/core/AppEntry.h, src/core/FuzzyMatcher.h, src/core/FuzzyMatcher.cpp, src/core/ResultsModel.h, src/core/ResultsModel.cpp, tests/tst_matcher.cpp, tests/tst_model.cpp]
  modified: [CMakeLists.txt]

key-decisions:
  - "MatchRangesRole shape locked for 03-05: QVariantList of two-int lists [{start,length}, ...] per contiguous matched run, positions into the original displayName — documented in ResultsModel.h"
  - "Result.score default-initialized to 0 so no-match / empty-query paths return the exact contract value {0,{}} (caught live: default-constructed Result carried 0xCCCCCCCC on the no-match path)"
  - "First matched char is tier-determining, so the subsequence scan prefers a word-boundary occurrence for it, then greedy-leftmost for the rest — boundary-tier beats subsequence-tier even when a mid-string plain match comes earlier"
  - "No gap penalty in scoring: space-vs-camelCase equality (D-04 'equally boundaries') is only preserved if boundary bonuses are flat per char (a raw skipped-char penalty would make Note Pad ≠ NotePad)"

patterns-established:
  - "Wisp-core registration: every new src/core/*.cpp source is listed in the qt_add_library(wisp_core STATIC) source list BEFORE use (Phase-2 LNK2019 lesson)"
  - "MSVC + QCOMPARE: braced-init-list arguments inside the QCOMPARE macro fail to parse (C2187/C2440) — hoist expected QVector values into named consts first"
  - "QtTest diagnostics on this machine: QtTest stdout buffers and is lost on abort/exit-1 under pwsh pipes — Start-Process -RedirectStandardOutput for failure detail"

requirements-completed: [LAUN-01, LAUN-05]

# Metrics
duration: 10min
completed: 2026-08-09
---

# Phase 3 Plan 1: AppEntry + FuzzyMatcher + ResultsModel Summary

**Tier-ladder fuzzy ranker (exact > prefix > word-boundary > subsequence, case-insensitive, camelCase bonuses) returning score + exact match ranges from day one, behind the shared AppEntry contract and a QAbstractListModel with alphabetical empty-query list, clamped keyboard selection, and the D-12 value-copy snapshot API — 15/15 new behaviors proven, full suite 7/7 green.**

## Performance

- **Duration:** 10 min
- **Started:** 2026-08-09T20:30:55Z
- **Completed:** 2026-08-09T20:40:27Z
- **Tasks:** 3 (1 auto + 2 TDD)
- **Files modified:** 8

## Accomplishments

- `AppEntry` shared struct — the 7-field phase contract (source/displayName/targetPath/arguments/aumid/iconRef) every other plan in Phase 3 consumes; iconRef reserved for Phase 5
- `FuzzyMatcher::score` — in-house ~150-line pure C++ matcher: tier ladder with construction-proof invariant (bonus cap below tier gap), word boundaries (start/separators/camelCase), exact match ranges with contiguous runs merged and gaps split, no cutoff, <5ms on the 500-name smoke (measured total suite 19ms)
- `ResultsModel` — QAbstractListModel with ranked query filtering (score desc, alphabetical tie-break D-05), cached match ranges for O(1) `data()` (D-06), clamped selection incl. ±7 page deltas (LAUN-05), value-copy snapshot (D-12 seed)
- Golden list proven in test: `cal`→Calculator, `term`→Terminal, `note`→Notepad rank first; tie-break determinism proven in matcher AND alphabetical ordering proven in model (Calc.exe before Calculator)

## Task Commits

Each task was committed atomically (TDD tasks as test → feat cycles):

1. **Task 1: AppEntry.h — shared phase entry contract** - `61a9a93` (feat)
2. **Task 2: FuzzyMatcher — golden list + match ranges** - `d02d8fd` (test, RED) + `7c68ccb` (feat, GREEN) + `08259c0` (refactor: MSVC-safe test vectors)
3. **Task 3: ResultsModel — full list, ranking, selection, snapshot** - `6d67ca4` (test, RED) + `d092945` (feat, GREEN)

**Plan metadata:** `docs(03-01): complete app-entry-fuzzy-matcher-results-model plan` (final commit)

## Files Created/Modified

- `src/core/AppEntry.h` - Shared entry struct; Source enum (Lnk/Uwp) + 5 fields; header-only contract
- `src/core/FuzzyMatcher.h` - MatchRange/Result structs + `score()` declaration; MatchRange operator== for QtTest; documented ladder semantics
- `src/core/FuzzyMatcher.cpp` - Linear per-char case-folded scan; tier constants 1000/800/600/400; boundary detection (start, space, -_/., camelCase); run merging; clamped bonus (T-03-01-01)
- `src/core/ResultsModel.h` - QAbstractListModel + Roles enum + Q_INVOKABLE selection API + snapshot; MatchRangesRole shape documented for 03-05
- `src/core/ResultsModel.cpp` - setEntries (alphabetical sort), setQuery (filter+rank, cached ranges), data() roles (Subtitle via QFileInfo), clamped selection, snapshot value copy
- `tests/tst_matcher.cpp` - 8 behaviors: golden list, ladder, camelCase, case-insensitivity, ranges exactness + in-bounds, no-cutoff, tie determinism, <5ms perf smoke
- `tests/tst_model.cpp` - 7 behaviors: empty-query full list D-01/D-02, query ranking with ranges, filter round-trip, selection bounds LAUN-05, snapshot freeze D-12, alpha tie-break D-05, subtitle role
- `CMakeLists.txt` - wisp_core sources + tst_matcher/tst_model targets in BUILD_TESTING

## Decisions Made

- MatchRangesRole QML shape fixed now (two-int lists) so 03-05 rendering and Phase-5 highlighting have a stable contract (D-07)
- Tier ladder as binary thresholds + capped bonuses — guarantees exact > prefix > boundary > subsequence unconditionally, not just for the test fixtures
- Boundary preference only for the FIRST matched char (tier-determining); rest greedy-leftmost — compact runs, boundary tier never missed when a later boundary occurrence exists

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Uninitialized Result.score on the no-match path**
- **Found during:** Task 2 GREEN verification (tst_matcher `noCutoff` FAIL)
- **Issue:** `Result none;` left `score` uninitialized — MSVC debug builds returned 0xCCCCCCCC (-858993460) instead of the contracted 0 for `score("x","NothingHere")` and empty queries
- **Fix:** Default member initializer `int score = 0;` in `FuzzyMatcher::Result` — every no-match path now returns the exact contract value `{0, {}}`
- **Files modified:** src/core/FuzzyMatcher.h
- **Verification:** tst_matcher 10/10 (incl. init/cleanup) pass via ctest
- **Committed in:** 7c68ccb (Task 2 GREEN commit)

**2. [Rule 1 - Bug] tst_model subtitleRole fixture assumed insertion order**
- **Found during:** Task 3 GREEN verification (tst_model `subtitleRole` FAIL)
- **Issue:** Test asserted row 0 = Notepad (Lnk) but the model correctly sorts alphabetically (D-01: Calculator < Notepad), so row 0 is the Uwp entry
- **Fix:** Corrected the test to assert the canonical order explicitly (row 0 Calculator → empty subtitle, row 1 Notepad → "notepad.exe")
- **Files modified:** tests/tst_model.cpp
- **Verification:** tst_model 9/9 pass; full suite 7/7
- **Committed in:** d092945 (Task 3 GREEN commit)

**3. [Rule 3 - Blocking] MSVC rejects braced-init-list inside QCOMPARE macro**
- **Found during:** Task 2 GREEN build (C2187/C2958/C2440 on `QCOMPARE(...ranges, QVector<MatchRange>{ {0,3} })`)
- **Issue:** MSVC's macro-expanded parse cannot handle `{ {0,3} }` as a macro argument — the test file never compiled even though the matcher was correct
- **Fix:** Hoisted expected vectors into named `const QVector<MatchRange>` locals before QCOMPARE
- **Files modified:** tests/tst_matcher.cpp
- **Verification:** tst_matcher builds and passes; ctest 7/7
- **Committed in:** 08259c0 (refactor commit)

---

**Total deviations:** 3 auto-fixed (2 Rule 1 bugs, 1 Rule 3 blocker)
**Impact on plan:** All three were correctness/build fixes within the task's own files — no scope creep, no contract changes.

## Issues Encountered

- QtTest failure diagnostics on this machine are swallowed under bare pwsh pipes (CRT buffer lost on abort) — resolved via `Start-Process -RedirectStandardOutput`; noted for future plans (already recorded in STATE phase-2 lessons as the known pattern)

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 03-02 (AppEntry produced by enumerators): struct contract ready — Lnk entries fill targetPath/arguments/iconRef, UWP fills aumid
- 03-03 (catalog aggregation): `AppCatalog` delivers `QVector<AppEntry>` straight into `ResultsModel::setEntries` (alphabetical sorting already owned by the model)
- 03-04 (launch): `snapshotSelected()` is the D-12 freeze seed; selection API covers ↑/↓/PageUp/PageDown/Home/End deltas (QML maps keys → moveSelection deltas ±1/±7)
- 03-05 (QML result list): DisplayNameRole/SubtitleRole/MatchRangesRole/AumidRole + `query` property for the empty-state copy all ready; MatchRangesRole shape documented in ResultsModel.h
- Phase-5 highlight rendering consumes `[{start,length}, ...]` ranges directly — LAUN-06 data contract born here as required (D-07)

---
*Phase: 03-app-search-result-model-app-catalog*
*Completed: 2026-08-09*

## Self-Check: PASSED

- 8/8 plan key files found on disk (7 code/test files + this SUMMARY)
- 6/6 task commits present in git log: 61a9a93, d02d8fd, 7c68ccb, 08259c0, 6d67ca4, d092945
- Full verification run: `build.ps1` clean; `ctest --test-dir build/dev` → 7/7 suites pass (tst_shell, tst_hotkey, tst_launcher, tst_capture, tst_matcher, tst_model, tst_tray)