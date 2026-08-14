# 07-04 Summary: D-01 backend swap — Windows Search deleted, local index + scan states live

**Status:** COMPLETE (3 tasks, ctest 20/20 green, suite count 21 → 20)
**Commits:** `abf545f` (T1 source), `b1eced2` (T1 tests), `4cbf9e8` + `9f3ed85` (T2 deletion), `f88d028` (T3)

## Task 1 — FileSearch remap + main.cpp rewiring (src/core/FileSearch.{h,cpp}, src/app/main.cpp, src/core/SettingsStore.{h,cpp})

- State enum `{Ok, Disabled, Building, Unavailable}` → `{Idle = 0, NoRoots, Scanning, Error}` — ordinal mirrors `ScanService::ScanState` (07-03); main.cpp maps EXPLICITLY via switch (never a blind cast).
- Skip-branch (D-16/D-17 spirit): NoRoots/Error skip the query (status-only result); Scanning still queries — the loaded index serves partial results mid-walk. Query failure with Idle status → Error.
- Locked copy single-homed in `statusText()`: `"No scan locations yet — add folders in Settings to search files"` / `"Scanning — files appear as they're found"` / `"Scan unavailable — check your scan locations in Settings"`; `indexerOk() == (state == Idle)`.
- main.cpp: includes swapped (WinSearchQuery → FileIndex/ScanService/WinDirectoryWalk); dedicated `scanPool` (max 1 thread — single-flight already serializes, D-08); `index.load()` synchronously at boot with `QElapsedTimer` A4 log, no scan at boot (D-09, Pitfall 8 degrade); QueryFn → `FileIndex::toEntries(index.queryCandidates(q), 100)`; StatusFn → scanService ordinal map; scan-service wiring after all fileSearch seams (listFn = winListDirectory, index, pool, UI-thread SettingsStore settingsSource snapshot, `start()`). Launch-history seams untouched (LAUN-03).
- SettingsStore: real live `scanRoots()` / `scanIntervalMinutes()` reads from `scan/roots` + `scan/intervalMinutes` (defaults: empty / 10) — 07-05 only needs the write side.

## Task 2 — Windows Search surface deleted (D-01)

- Deleted `src/win/WinSearchQuery.{h,cpp}` (OLE DB COM pipeline) + `tests/tst_search.cpp`; CMakeLists: wisp_core source line, tst_search block, ENVIRONMENT_MODIFICATION list. Whole-repo grep `WinSearchQuery|tst_search` → 0 matches (even the WinIconExtractor.cpp prose comment reworded).

## Task 3 — MainWindow.qml D-04 no-roots prompt

- New `noRootsPrompt` Item between emptyState and statusRow: gate `resultsModel.query === "" && !fileSearch.indexerOk && resultsView.count === 0` — mutually exclusive with line 708 (`indexerOk` gate) and line 752 (`query !== ""` gate). Renders `fileSearch.statusText` verbatim; same glyph + two-line structure as emptyState.

## Verification

- Build clean (qmlcachegen validates MainWindow.qml); ctest **20/20 green** (`-R "tst_filesearch|tst_model"` + full suite after each task).

## Notes for later plans

- 07-05 consumes the SettingsStore read accessors (already live) and adds the write path from the Settings UI; Theme.qml settings-window height growth (360→560) still pending.
- Manual smoke (phase VALIDATION list): launch wisp with no roots → D-04 prompt on empty query; type → status-row copy; index load timing appears in the log.
