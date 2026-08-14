# 07-03 Summary: ScanService orchestration (single-flight scan loop + state machine)

**Status:** COMPLETE (2 tasks, ctest 21/21 green)
**Commits:** `0a0f06e` (Task 1), `a844bcd` (Task 2)

## Task 1 — src/core/ScanService.{h,cpp} + CMakeLists

- **Single-flight scan loop**: `m_scanInFlight` gate + `m_rescanPending` coalescing — concurrent `requestScan()`/timer ticks collapse into exactly one follow-up scan, never a queue.
- **Worker discipline (AppCatalog precedent)**: worker lambda sets `QThread::LowPriority` and wraps the walk in try/catch — nothing escapes QtConcurrent; the walk runs on the injected `QThreadPool` (`setPool`, dedicated per-process pool for the app) or QtConcurrent's global pool as the test fallback.
- **Pitfall 4 (UI-thread snapshot)**: settings read once, on the UI thread, via the injected `SettingsSource` lambda — the worker only touches the copied snapshot.
- **No scan at boot (D-09)**: `start()` arms the QTimer only when roots exist; empty roots → `NoRoots` state, timer stopped. Instant relaunch relies on the persisted index (FileIndex load), not a startup walk.
- **Interval ticks**: QTimer → `requestScan()` (same gate), timer re-armed from a FRESH snapshot after each completion; `refreshInterval()` re-reads settings and re-arms; interval clamped 1–1440 min at every consumption site (`clampedInterval`).
- **State machine**: ordinal `Idle / NoRoots / Scanning / Error` + `lastScanSummary` ("Last scan HH:mm — N entries · K location failed") published via `Q_PROPERTY(... NOTIFY scanStateChanged)` for the main.cpp `StatusFn` seam; emit only when state or summary changed.
- **State mapping**: roots empty → `NoRoots`; any `failedListings` → `Error`; else `Idle` (post-completion mapping in `onScanFinished`, which also applies + saves the outcome on the UI thread).
- **CMakeLists**: `ScanService.cpp` added to `wisp_core` after FileIndex.cpp (matching the scan ordering in the app source list).

## Task 2 — tests/tst_scan.cpp ScanService suites (7 new slots)

- `scanPopulatesIndexAndSummary` — fake map walk (2 indexable entries), entryCount + summary format + Idle.
- `singleFlightCoalescesConcurrentScans` — slow-first-call fake; **per-path call counts discriminate serial coalescing (root=2, sub=1, total=3) from broken concurrent single-flight (total=4)**; index end-state consistent; ≥1 Scanning→Idle round trip.
- `noRootsClearsIndexAndState` — roots removed → next scan wipes index, state `NoRoots` (D-09 no-locations semantics, end-to-end).
- `failedListingMapsErrorState` — unreadable fake → `Error` + "1 location failed" summary.
- `snapshotReadOnUiThread_Pitfall4` — settings source records `QThread::currentThreadId()`; asserts equality with the UI (test) thread.
- `startArmsTimerOnlyWithRoots` — empty roots → `NoRoots` + single emit, no scan; roots present → timer armed, ZERO listFn calls (D-09), no spurious NOTIFY.
- `refreshIntervalReadsFreshSnapshot` — two calls → two snapshot reads, state untouched.
- Uses the dedicated `m_pool` (WR-05) and per-test `QTemporaryDir` isolation; `QTRY_COMPARE_WITH_TIMEOUT` for poll-based waits.

## Verification

- Build clean; `tst_scan` 14/14 via `-o file,txt` harness; full `ctest` 21/21 green (no stray wisp.exe).

## Notes for later plans

- **07-03 contract gap fixed in Task 1**: `walkAndDelta` with empty roots previously removed nothing (removals only computed for re-listed dirs) — added an explicit WIPE path (all snapshot keys removed, memo cleared) so a re-added root re-walks from scratch. Covered end-to-end by `noRootsClearsIndexAndState`.
- First test run exposed a test-design race (spy.wait after QTRY already consumed the final emit) — replaced with `QTRY_COMPARE` on the terminal state.
- 07-04 consumes `ScanService` in main.cpp (lines 24, 98-122) with `setListFn(WinDirectoryWalk::winListDirectory)` + `setSettingsSource` from SettingsStore `scan/` keys; FileSearch still owns the state plumbing until then.
