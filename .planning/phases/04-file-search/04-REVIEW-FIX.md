---
phase: 04-file-search
fixed_at: 2026-08-10T05:52:45+03:30
review_path: .planning/phases/04-file-search/04-REVIEW.md
iteration: 1
findings_in_scope: 5
fixed: 5
skipped: 0
status: all_fixed
---

# Phase 4: Code Review Fix Report

**Fixed at:** 2026-08-10T05:52:45+03:30
**Source review:** .planning/phases/04-file-search/04-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 5
- Fixed: 5
- Skipped: 0

## Fixed Issues

### WR-01: LaunchHistory concurrent access to QSettings is unsynchronized

**Files modified:** `src/core/LaunchHistory.h`, `src/core/LaunchHistory.cpp`, `tests/tst_history.cpp`
**Commit:** 3ce32d0
**Status:** fixed: requires human verification
**Applied fix:** Added a `mutable QMutex` to LaunchHistory guarding `recordLaunch`, `addExecutable`, `trackedExecutables`, and `launchCount`. The launch-count read is inlined inside `recordLaunch` under the same lock (QMutex is non-recursive — `launchCount()` cannot be called from within the locked section). Added `concurrentAccessThreadSafe_WR01` regression test: 250 writers + 250 readers via QtConcurrent, no assertion failures or crashes.

### WR-02: OLE DB row reads ignore status slots and length bounds

**Files modified:** `src/win/WinSearchQuery.cpp`
**Commit:** 740e14c
**Status:** fixed: requires human verification
**Applied fix:** The row loop now requires `DBSTATUS_S_OK` for all three columns (path, name, folder) before reading a slot — any other status means the slot was never written, so the row is skipped. Reads are bounded by the length slots (`RowLayout::kPathMax` / `kNameMax` / `kFolderMax`); rows whose values reach the capacity limit are skipped as truncated. The folder flag compare is length-bounded (`_wcsnicmp` over the slot's char count, with NUL handling).

### WR-03: Search results can go stale — results from an old query may be displayed

**Files modified:** `src/core/FileSearch.h`, `src/core/FileSearch.cpp`, `src/core/ResultsModel.h`, `src/core/ResultsModel.cpp`, `src/app/main.cpp`, `tests/tst_model.cpp`
**Commit:** 7bb2387
**Status:** fixed: requires human verification
**Applied fix:** The query text is now carried end-to-end: `FileSearch::WorkerResult` gained a `query` field and the signal became `resultsReady(quint64 generation, const QString &query, const QVector<AppEntry> &files)`. Both `FileSearch::onFinished` and `ResultsModel::setFileResults` drop deliveries whose text does not match the current query; `setFileResults` checks text before the generation guard. `ResultsModel::setEntries` no longer resets `m_fileGeneration`, so the generation counter stays monotonic across refreshes. `main.cpp` wiring updated; existing model tests migrated to the 3-argument signature; new tests `staleTextDropped_WR03` and `generationGuardMonotonicAcrossRefresh_WR03`.

### WR-04: Status row can cover search results

**Files modified:** `qml/MainWindow.qml`
**Commit:** b202ab7
**Status:** fixed
**Applied fix:** The status row's `visible` now also requires `resultsView.count === 0`, so it is hidden whenever results are shown and can no longer overlap them (in addition to the existing query-nonempty and indexer-failed conditions).

### WR-05: Timing-sensitive tests rely on fixed sleeps and the shared global thread pool

**Files modified:** `src/core/FileSearch.h`, `src/core/FileSearch.cpp`, `tests/tst_filesearch.cpp`
**Commit:** 1809d5a
**Status:** fixed
**Applied fix:** Added a `setPool(QThreadPool*)` seam to FileSearch (falls back to `QThreadPool::globalInstance()` when unset; FileSearch never owns the pool). Every test now runs its dispatches on a dedicated pool, removing cross-test interference from the shared global pool. Rewrote tst_filesearch.cpp to assert on signal deliveries (`QSignalSpy::wait` with a generous 5 s timeout) instead of fixed sleeps; short `qWait`s remain only for negative (nothing-fires) assertions. Fixed `addExecutableRedispatches_D11` to assert the immediate re-dispatch's delivery within the quiet window — Qt 6 `QSignalSpy::wait()` only waits for a NEW signal, so the second delivery (already recorded during the preceding `qWait`) was timing the wait out. Full suite green after the rewrite: 13/13 passing.

---

_Fixed: 2026-08-10T05:52:45+03:30_
_Fixer: OpenCode (gsd-code-fixer)_
_Iteration: 1_
