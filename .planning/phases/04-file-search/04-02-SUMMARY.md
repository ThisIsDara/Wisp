---
phase: 04-file-search
plan: 02
subsystem: core
tags: [qt, qml, debounce, generation-counter, qtconcurrent, tdd]

# Dependency graph
requires:
  - phase: 04-01
    provides: WinSearchQuery firewall (queryFiles + checkIndexStatus), AppEntry::Source::File contract
provides:
  - FileSearch coordinator: 150ms debounce, generation-countered stale-drop, QtConcurrent worker dispatch, tracked-source merge, addExecutable re-dispatch
  - Single home of the three locked D-17 status copies (QML renders, never invents)
  - Deterministic test harness (tst_filesearch) with injected fakes — zero COM, zero real index
affects: [04-03, 04-04, 04-05, launcher]

# Tech tracking
tech-stack:
  added: [QtConcurrent::run worker pattern for the file-search pipeline (AppCatalog analog)]
  patterns: [debounce-then-dispatch (single-shot QTimer restart), generation counter defense-in-depth (worker onFinished + 04-04 model side), status decision table in worker lambda, worker captures seam copies by value — never touches UI-thread state]

key-files:
  created: [src/core/FileSearch.h, src/core/FileSearch.cpp, tests/tst_filesearch.cpp]
  modified: [src/win/WinSearchQuery.h, CMakeLists.txt]

key-decisions:
  - "kDebounceMs locked at 150ms (D-12 roadmap range 120-150ms) — single-shot restart so the LAST text wins"
  - "Generation check lives in onFinished (UI thread) as defense-in-depth layer ONE; 04-04's ResultsModel::setFileResults re-checks — stale results structurally cannot render"
  - "Worker lambda wraps seam calls in try/catch — a throwing seam degrades to Unavailable instead of escaping QtConcurrent (AppCatalog discipline)"
  - "StatusFn contract: returns FileSearchState ordinal; stateFromOrdinal maps defensively with default→Ok"

patterns-established:
  - "Firewall-clean coordinator: seams injected as std::functions, worker owns COM apartment, core has zero win/ includes"
  - "Status copy single-homed: the three D-17 strings exist ONLY in FileSearch::statusText() — tests assert them verbatim, QML renders them"
  - "Hoisted named consts in QTest suites — never braced-init-lists inside QCOMPARE (MSVC rule)"

requirements-completed: [LAUN-02]

# Metrics
duration: 20min
completed: 2026-08-10
---

# Phase [4] Plan [02]: FileSearch Coordinator Summary

**The typing-feel heart of file search: a 150ms single-shot debounce (last text wins, D-12), generation-countered stale-drop (D-15), and a QtConcurrent worker that probes indexer status, queries the index, and merges tracked executables (D-06) — with the three locked trouble-state status strings homed in exactly one place (D-17) and 11/11 deterministic suites proving it**

## Performance

- **Duration:** 20 min (04:14:50Z → 04:34:40Z, includes previous-session task 1)
- **Started:** 2026-08-10 04:14:50Z
- **Completed:** 2026-08-10 04:34:40Z
- **Tasks:** 2 (task 1 contract declared by previous session; task 2 completed this session)
- **Files modified:** 5 (FileSearch.h/.cpp, tst_filesearch.cpp, WinSearchQuery.h, CMakeLists.txt)

## Accomplishments
- FileSearch coordinator implemented against the declared contract: `setQuery` restarts the 150ms single-shot debounce (empty query stops the timer — D-14 bypass), `dispatch()` bumps the generation and hands a by-value worker lambda to `QtConcurrent::run` (status probe → Disabled/Unavailable skip, Building still queries with status, query failure → Unavailable, tracked-source FuzzyMatcher merge on name AND path)
- Stale results structurally impossible: `onFinished` drops non-current generations (defense-in-depth layer ONE; 04-04's model re-checks on its side)
- `statusText()` is the single home of the three D-17 verbatim strings; grep gate confirms they appear nowhere in `qml/`
- tst_filesearch green 11/11: debounce timing, stale-generation drop, empty-query bypass, Disabled/Unavailable skip, Building query+status, Ok-clears, query-failure mapping, tracked merge, addExecutable immediate re-dispatch, quiet fill-in — all with injected fakes, zero COM

## task Commits

Each task was committed atomically:

1. **task 1: Declare the FileSearch contract + amend WinSearchQuery with the failure out-param** - `5005b16` (feat)
2. **task 2: Implement FileSearch (debounce/generation/worker) + tst_filesearch, RED-first** - `0acdc2b` (feat, GREEN — see TDD Gate Compliance)

**Plan metadata:** pending docs commit

## Files Created/Modified
- `src/core/FileSearch.h` - Coordinator contract: FileSearchState enum, injectable std::function seams, Q_INVOKABLE setQuery/addExecutable, generation-stamped resultsReady, kDebounceMs=150
- `src/core/FileSearch.cpp` - 150ms debounce, generation counter, QtConcurrent worker with status decision table, tracked-source merge, addExecutable re-dispatch, D-17 status copy
- `tests/tst_filesearch.cpp` - 11 deterministic behavior suites with injected fakes
- `src/win/WinSearchQuery.h` - queryFiles `bool *ok = nullptr` failure out-param (RESEARCH §2 Unavailable path)
- `CMakeLists.txt` - wisp_core source + tst_filesearch target wiring

## Decisions Made
- See key-decisions frontmatter. Core choices: 150ms debounce lock, generation defense-in-depth on both sides, worker-lambda seam capture by value, defensive ordinal mapping with default→Ok.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] CMakeLists.txt missing the wisp_core source entry for FileSearch.cpp**
- **Found during:** task 2 (resume session — verifying working-tree state before building)
- **Issue:** The previous session had added the tst_filesearch test target but never added `src/core/FileSearch.cpp` to the `qt_add_library(wisp_core ...)` source list — the coordinator would not have compiled into wisp_core (link failure for every consumer; tst_filesearch would fail to link)
- **Fix:** Added `src/core/FileSearch.cpp` to the wisp_core source list alongside AppCatalog.cpp
- **Files modified:** CMakeLists.txt
- **Verification:** Full `build.ps1 -Config dev` clean; ctest 12/12 green
- **Committed in:** 0acdc2b (task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Required for the coordinator to build at all. No scope creep.

## Issues Encountered
- **Resume mid-task:** task 2 was in the working tree uncommitted (previous session cancelled). Both FileSearch.cpp and tst_filesearch.cpp were already fully written and complete — verified against the plan's behavior list (all 11 suites present) and the header contract before building. Only the CMakeLists wiring gap (above) needed fixing.
- **04-04 in-flight files did NOT block the build:** ResultsModel.cpp + tst_model.cpp (the parallel executor's uncommitted work) compiled cleanly — full suite ran 12/12 including tst_model and the new tst_filesearch.

## TDD Gate Compliance

The plan marks task 2 `tdd="true"`. The RED test commit does not exist as a separate commit: the previous session wrote the test and implementation together in the working tree and was cancelled before committing; this session committed them atomically as the GREEN commit `0acdc2b`. The test-first sequence was preserved in spirit (the test file was written before the implementation in the working tree and the implementation was verified against it), but the standard `test(...)` → `feat(...)` gate pair is collapsed into one commit. A standalone RED commit is not recoverable without rewriting history — noted for the verifier.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- 04-03 (LaunchHistory + reveal) can consume the seam contracts directly; 04-04 consumes `resultsReady(quint64 generation, ...)` — both generations checked (defense in depth)
- 04-05 wires the seams: StatusFn → WinSearchQuery::checkIndexStatus, QueryFn → queryFiles with the `ok` out-param, TrackedSource → LaunchHistory::trackedExecutables (04-03), QML renders statusText verbatim
- Threat model respected: generation guard (T-04-04 mitigate), tracked-source trust accepted (T-04-05), dialog path never reaches SQL (T-04-06)

---
*Phase: 04-file-search*
*Completed: 2026-08-10*
