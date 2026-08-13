---
phase: 03-app-search-result-model-app-catalog
plan: 03
subsystem: core
tags: [qt6, qtconcurrent, qfuturewatcher, threading, com, dedupe, qtest]

# Dependency graph
requires:
  - phase: 03-app-search-result-model-app-catalog (03-01)
    provides: AppEntry struct (Source enum, displayName, targetPath, arguments, aumid, iconRef) + ResultsModel::setEntries consumer contract
  - phase: 03-app-search-result-model-app-catalog (03-02)
    provides: WinStartMenuEnumerator::scanStartMenu + WinUwpEnumerator::scanUwpApps signatures (wired via std::function Scanner by 03-05, faked in tests)
provides:
  - AppCatalog: worker-thread-built, age-refreshed (10-min default), silently-swapped, Lnk-wins-deduped in-memory app list — start()/ensureFresh()/entries()/setScanners()/setRefreshInterval() + refreshed()/buildFailed() signals
  - Contract consumed by 03-04 (launch) + 03-05 (main.cpp wiring → ResultsModel::setEntries)
affects: [03-04-launch, 03-05-result-list-ui]

# Tech tracking
tech-stack:
  added:
    - "Qt6::Concurrent module (QtConcurrent::run + QFutureWatcher) — find_package Concurrent + PUBLIC link on wisp_core, linked transitively by tst_catalog"
  patterns:
    - "Worker-owns-COM-apartment batch: CoInitializeEx(COINIT_MULTITHREADED) at worker-lambda start / CoUninitialize at end, with S_FALSE + RPC_E_CHANGED_MODE reuse discipline copied from WinStartMenuEnumerator (PITFALLS #3) — no WinRT headers in the catalog TU"
    - "Single-flight build gate: overlapping start()/ensureFresh() coalesce; exactly one refreshed() per swap (T-03-03-02)"
    - "Mutex-guarded implicit-shared snapshot: readers copy the QVector under QMutex (const method → mutable mutex), writer swaps under the same lock — reads never block and never see partial lists (T-03-03-01); m_buildInFlight/m_lastBuilt/m_interval are UI-thread-only"

key-files:
  created: [src/core/AppCatalog.h, src/core/AppCatalog.cpp, tests/tst_catalog.cpp]
  modified: [CMakeLists.txt]

key-decisions:
  - "QtConcurrent::run + QFutureWatcher<ScanResult> over a dedicated QThread — least machinery, completion marshaled to the UI thread by the watcher's event loop; ScanResult{entries, errorCount} carries per-scanner failure counts across the thread boundary"
  - "Single QVector under QMutex (rather than live+staging pairs): writer builds into a local, then locks and moves — the swap IS the atomic step (implicit sharing makes the reader copy O(1))"
  - "CoInitializeEx MTA instead of winrt::init_apartment in the catalog worker — identical underlying call (RESEARCH §2 'pick one and use it everywhere'), keeps the cppwinrt include confined to WinUwpEnumerator.cpp per the plan's note"
  - "Dedupe implemented as two-pass Lnk-first + toCaseFolded() key rejection of UWP duplicates — .lnk-vs-.lnk and Uwp-vs-Uwp duplicates are intentionally NOT deduped (D-10 covers cross-source collisions only)"
  - "ensureFresh() treats 'never built' as stale (epoch timestamp) — a defensive first build if main.cpp ever calls ensureFresh before start(); no behavioral cost on the hotkey path"
  - "Test-1 fixture packs the case-sensitivity probe (UWP 'CalculatorX') into the same build as the collision (UWP 'calculator'): 4 entries [Calculator(Lnk), CalculatorX(Uwp), Photos(Uwp), Terminal(Lnk)] assert suppressed-collision + non-colliding-remain + case-sensitive-kept + Lnk-wins in one deterministic build"

patterns-established:
  - "QtConcurrent worker + COINIT_MULTITHREADED apartment wrap: the catalog worker lambda initializes BEFORE invoking scanners and uninitializes AFTER (documented: both WinRT scanners assume an initialized apartment)"
  - "Test-hook interval: setRefreshInterval(50 ms) + QTest::qWait(80) ages the catalog deterministically on steady_clock — no protected timestamp setter needed"

requirements-completed: [LAUN-01]

# Metrics
duration: 4min
completed: 2026-08-10
---

# Phase 3 Plan 3: AppCatalog Summary

**QtConcurrent::run worker-thread app catalog with single-flight builds, 10-minute steady-clock age refresh (default interval, test-hookable), .lnk-wins dedupe on exact case-insensitive full-name collisions (D-10), and a QMutex-guarded implicit-shared snapshot swap that keeps readers on the old consistent version mid-rebuild (D-08 silent swap) — all six injected-scanner behaviors proven by tst_catalog, full suite 9/9 green.**

## Performance

- **Duration:** ~4 min
- **Started:** 2026-08-10T00:41:52Z
- **Completed:** 2026-08-10T00:46:06Z
- **Tasks:** 1 (auto, tdd="true" — RED + GREEN)
- **Files modified:** 4 (3 created, 1 modified)

## Accomplishments

- `AppCatalog` per the plan's interfaces block exactly: `start()` (never in the hotkey path), `ensureFresh()` (cheapest possible age check — rebuild only when older than the interval), `entries()` (any-thread, O(1) implicit-shared copy under lock), `setScanners`/`setRefreshInterval` injection, `refreshed()`/`buildFailed(int)` signals
- Worker-thread build off the hotkey path via `QtConcurrent::run`; the worker lambda owns its COM apartment (CoInitializeEx MTA at batch start, CoUninitialize at end, with the S_FALSE/RPC_E_CHANGED_MODE reuse discipline from 03-02) — no WinRT headers in the TU
- Single-flight build gate: overlapping `start()`/`ensureFresh()` coalesce; exactly one `refreshed()` per swap (T-03-03-02) — proven by both the ageRefresh negative case and the blocking-scanner swap test
- Silent atomic swap: readers copy under QMutex, writer swaps under the same lock — the blocking-scanner test (200 ms fake) proves readers see the old snapshot whole mid-rebuild, then the new list after one `refreshed()` (T-03-03-01)
- Dedupe D-10 + alphabetical sort D-03 (same `toCaseFolded()` comparator as ResultsModel) proven with injected fakes — a UWP duplicate of an .lnk name is suppressed, case-sensitive lookalikes and UWP-only apps survive
- Failure isolation (T-03-03-03): a throwing scanner is counted (`buildFailed(1)`) and skipped; the previous catalog stays intact; no crash
- Startup non-blocking proof (PITFALLS #14): `entries()` immediately after `start()` returns an empty-but-valid snapshot while a slow scan is still sleeping

## Task Commits

Each task was committed atomically (TDD RED → GREEN):

1. **Task 1: AppCatalog policy — worker build, age refresh, silent swap, dedupe (D-08..D-10)** — `538bb4f` (test, RED) + `f23c14b` (feat, GREEN)

**Plan metadata:** `docs(03-03)` (final commit)

## Files Created/Modified

- `src/core/AppCatalog.h` - Interface per the plan's interfaces block + documented threading contract (worker COM ownership, mutex-guarded swap, UI-thread-only build state); QFutureWatcher<ScanResult> member
- `src/core/AppCatalog.cpp` - buildAsync (single-flight + CoInitializeEx-wrapped scanner batch), onBuildFinished (dedupe → sort → lock-and-swap → buildFailed/refreshed), dedupeLnkOverUwp (Lnk-first, toCaseFolded keys), sortAlphabetical (D-03 comparator)
- `tests/tst_catalog.cpp` - 6 behaviors: dedupePrecedence_D10, alphabeticalOrder_D03, ageRefresh_D08 (positive + negative), silentSwapConsistency_D08 (200 ms blocking scanner), buildFailureIsolation, startupIsNonBlocking
- `CMakeLists.txt` - find_package += Concurrent; wisp_core += AppCatalog.cpp + PUBLIC Qt6::Concurrent; tst_catalog target in BUILD_TESTING (mirrors tst_hotkey)

## Decisions Made

- QtConcurrent + QFutureWatcher over a dedicated QThread (plan-preferred, "least machinery"); the watcher lives on the UI thread so the swap always runs there
- Single QVector under QMutex = the swap's atomic step; readers never pay for a lock-held copy beyond the implicit-share refcount (documented in header)
- CoInitializeEx MTA (== winrt::init_apartment(multi_threaded) underlying call) keeps WinRT headers confined to WinUwpEnumerator.cpp per the plan's note
- Dedupe scope kept exact per D-10: cross-source Lnk-vs-Uwp only, exact full-name equality, no fuzzy matching (T-03-03-04)

## Deviations from Plan

### Auto-fixed Issues

> Out of scope by design: the plan's Test-1 fixture (3 entries) was exercised with the case-sensitivity probe folded into the same build, so `entries()` yields 4 entries — every claim of the plan's Test 1 is asserted (see decision note). No behavior deviates from the plan.

**1. [Rule 1 - Bug] tst_catalog dedupe fixture expected-list count wrong**
- **Found during:** Task 1 GREEN verification (tst_catalog run: dedupePrecedence_D10 FAIL)
- **Issue:** The test expected 3 entries and dropped "Photos" from the expected list — but Photos is the plan's *non-colliding* UWP-only app that must remain. The implementation (correctly) returned 4: [Calculator(Lnk), CalculatorX(Uwp), Photos(Uwp), Terminal(Lnk)]
- **Fix:** Corrected the expectation to 4 entries with per-row source assertions (Lnk-wins, case-sensitive kept, UWP-only remains)
- **Files modified:** tests/tst_catalog.cpp
- **Verification:** tst_catalog 6/6 via ctest; full suite 9/9
- **Committed in:** f23c14b (GREEN commit)

**2. [Rule 3 - Blocking] QMutexLocker cannot lock a non-mutable QMutex in `entries() const`**
- **Found during:** Task 1 GREEN build (qmutex.h C2280/C2440 on `QMutexLocker<const QMutex>`)
- **Issue:** `entries()` is const but takes a read lock; Qt 6's QMutexLocker template instantiated with `const QMutex` and refused to compile
- **Fix:** Declared `mutable QMutex m_snapshotMutex` (documented: readers take a lock inside a const method)
- **Files modified:** src/core/AppCatalog.h
- **Verification:** build clean; ctest 9/9
- **Committed in:** f23c14b (GREEN commit)

---

**Total deviations:** 2 auto-fixed (1 Rule 1 bug, 1 Rule 3 blocker)
**Impact on plan:** Both fixes were within the task's own files — one test expectation correction and one compiler-level constness fix. No contract changes, no scope creep.

## Issues Encountered

- QtTest stdout swallowed under pwsh pipes again (known machine behavior) — diagnosed with QtTest's own `-o <file>,txt` (the 03-02 recipe); exit-code routing through ctest confirmed the pass state
- `rg` is not installed on this machine — plan's grep-style verification checks were executed with `Select-String` equivalents (same patterns, same results)

## TDD Gate Compliance

- RED gate: `538bb4f` test(03-03) — build failed with `C1083: Cannot open include file: 'core/AppCatalog.h'` (fails for the right reason: feature missing), committed before any implementation
- GREEN gate: `f23c14b` feat(03-03) — implementation lands after the test commit in git history; test passes 6/6 behaviors
- REFACTOR: none needed — GREEN implementation was minimal and documented; no cleanup pass required
- Order verified in git log: test(03-03) → feat(03-03), no violation

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 03-04 (launch) consumes `snapshotSelected()` (already ready from 03-01) — catalog is just data; no launch-facing surface in AppCatalog
- 03-05 (main.cpp wiring): `AppCatalog catalog; catalog.setScanners({&WinStartMenuEnumerator::scanStartMenu, &WinUwpEnumerator::scanUwpApps}); catalog.start();` at boot; `conn<freshed → resultsModel.setEntries(catalog.entries())`; `ensureFresh()` on each shell show (hotkey path pays only the age compare + snapshot copy)
- Scan-order note for 03-05: scanners run in vector order; dedupe is Lnk-first by construction so scanner order does not affect D-10 outcome
- No blockers; threat register T-03-03-01..05 all mitigated and test-proven

---
*Phase: 03-app-search-result-model-app-catalog*
*Completed: 2026-08-10*

## Self-Check: PASSED

- 4/4 plan key files found on disk: src/core/AppCatalog.h, src/core/AppCatalog.cpp, tests/tst_catalog.cpp, 03-03-SUMMARY.md
- 3/3 commits present in git log: 538bb4f (test RED), f23c14b (feat GREEN), c68fc06 (SUMMARY docs)
- Full verification run: `build.ps1` clean; `ctest --test-dir build/dev` → 9/9 suites pass (tst_shell, tst_hotkey, tst_launcher, tst_capture, tst_matcher, tst_model, tst_enum, tst_catalog, tst_tray) — no regressions
- Grep checks: no SQLite/QSqlDatabase in AppCatalog.{h,cpp} (D-09); QtConcurrent::run present (worker route); no AppCatalog/ResultsModel references in WinHotkey.cpp/HotkeyManager.cpp (off hotkey path)