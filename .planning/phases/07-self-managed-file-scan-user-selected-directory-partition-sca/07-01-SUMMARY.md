# 07-01 Summary: WinDirectoryWalk + FileIndex + Test Suites

**Status:** COMPLETE (all tasks done, ctest 21/21 green)
**Commits:** `c09149a` (Task 1), `d2d1ade` (Task 2), `05684fb` (Task 3)

## Task 1 — WinDirectoryWalk (src/win)

- `WinDirListing`/`WinDirEntry` plain-Qt structs; zero Win32 types in the header; zero COM (kernel32-only, D-08).
- `FindFirstFileExW` + `FindExInfoBasic` + `FIND_FIRST_EX_LARGE_FETCH`; `\\?\` long-path prefix for paths > 260 chars (UNC form included); 32000-char hard cap with error state, never a hang.
- Attributes (hidden/system/reparse), mtime from find data only — no per-entry `GetFileAttributesExW` (RESEARCH Pitfalls 1/2); single shared FILETIME→ms conversion (memo comparability).
- Directory mtime from the `.` find result (zero extra calls).

## Task 2 — FileIndex (src/core)

- `WalkOutcome` delta contract: `added`, `removed` (folded native paths), full `mtimes` memo, `dirsListed`/`failedListings`.
- `walkAndDelta` (const, read-locked, worker-safe): re-lists only dirs whose memo mtime changed; unchanged subtrees get zero list calls. Removals computed per re-listed dir against its direct children; deleted-dir subtrees swept wholesale (no orphans).
- Skip list (15 fixed names, case-insensitive leaf match) + hidden/system/reparse excluded from index AND descent. `.exe` + dirs only (D-02).
- Persistence: `wisp-index.dat` (AppDataLocation), magic `0x57535031` + version 1, `QSaveFile` atomic commit (D-07). Corrupt/truncated/foreign → `load()` false, empty index, no crash.
- `queryCandidates`: folded-subsequence prefilter (superset of FuzzyMatcher, A3), cap 100 (research OQ5), empty query → `{}` (D-14). `toEntries` static builder (Source::File).
- `apply()` dedupes by folded native path (overlapping roots first-wins); all state under one mutex (walk + query + save are cross-thread safe; single-flight scan makes worker mutation impossible).

## Task 3 — Tests (tests/tst_scan.cpp, tests/tst_index.cpp)

- **tst_index** (fake listing map, no Win32): incremental memo (unchanged subtree → 1 dir listed), add/remove/rename deltas, dir-deletion subtree sweep (4→1 entries), failed-listing keeps old data + counts failures, skip-list blocks descent, hidden exclusion, cap-100 + empty-query + subsequence semantics, persistence round-trip incl. memo (no re-descent after reload), corrupt file tolerated, concurrent query-during-walk smoke (std::thread, 50 walks).
- **tst_scan** (real seam, QTemporaryDir, per-test isolation via `unique_ptr`): .exe+folders indexing vs dll/txt exclusion, real `FILE_ATTRIBUTE_HIDDEN` honored via find data, `node_modules` real-dir skip proof, save/reload requery, 4096-byte garbage index tolerated.
- Both registered in CMakeLists (19 → 21 suites) with the shared `PATH` ENVIRONMENT_MODIFICATION.

## Verification

- `powershell -ExecutionPolicy Bypass -File .\build.ps1` — clean build.
- `ctest --test-dir build/dev` — **21/21 passed**. (tst_hotkey/tst_singleinstance failures were a stray dev `wisp.exe` holding the hotkey + single-instance lock; killed, re-ran, green.)

## Deviations

- None. Task split followed the plan's commit contract (WinDirectoryWalk committed first as its own unit; FileIndex; then tests).

## Notes for later plans

- tst_search/tst_filesearch still reference WinSearchQuery (deleted in 07-04 — those suite counts drop to 20 then).
- `FileIndex::toEntries` is the seam 07-03's ScanService and 07-04's FileSearch will consume.
- Executor subagents returned empty results twice on this Windows host — executed inline by the orchestrator (workflow fallback); no plan deviation.
