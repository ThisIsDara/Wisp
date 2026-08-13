---
phase: 04-file-search
reviewed: 2026-08-10T06:00:00Z
depth: standard
files_reviewed: 21
files_reviewed_list:
  - CMakeLists.txt
  - qml/MainWindow.qml
  - qml/ResultsRow.qml
  - src/app/main.cpp
  - src/core/AppEntry.h
  - src/core/FileSearch.h
  - src/core/FileSearch.cpp
  - src/core/LaunchController.h
  - src/core/LaunchController.cpp
  - src/core/LaunchHistory.h
  - src/core/LaunchHistory.cpp
  - src/core/ResultsModel.h
  - src/core/ResultsModel.cpp
  - src/win/WinLaunch.h
  - src/win/WinLaunch.cpp
  - src/win/WinSearchQuery.h
  - src/win/WinSearchQuery.cpp
  - tests/tst_search.cpp
  - tests/tst_filesearch.cpp
  - tests/tst_history.cpp
  - tests/tst_launch.cpp
  - tests/tst_model.cpp
findings:
  critical: 0
  warning: 5
  info: 4
  total: 9
status: issues_found
---

# Phase 4: Code Review Report

**Reviewed:** 2026-08-10
**Depth:** standard
**Files Reviewed:** 21
**Status:** issues_found

## Summary

Reviewed the phase-04 file-search vertical slice: the raw OLE DB COM firewall (WinSearchQuery), the debounced generation-countered coordinator (FileSearch), the launch-history INI store + Explorer reveal (LaunchHistory/WinLaunch), the merged ResultsModel, the main.cpp wiring, the QML UX, and all six test suites.

The architecture is sound and the defense-in-depth story is mostly real: generation checks exist on both sides, COM apartment discipline is correct (CoInitializeEx MTA per call on the worker, S_FALSE/RPC_E_CHANGED_MODE tolerated), the debounce is a correct single-shot restart, the 5-file cap is enforced in the merge, D-05's elevation mapping is provably unreachable for file rows, and the reveal path quotes/normalizes its argument with no shell involved. The test fakes genuinely exercise the seams (zero COM in CI).

The defects found are in four areas: (1) a **data race on the shared QSettings instance** between the worker thread and the UI thread — the one finding that can corrupt persisted state; (2) **row-buffer handling in the OLE DB GetData loop** that can surface garbage rows or over-read when a binding fails or fills its slot; (3) **generation counters validate ordering, not query text** — stale rows from an older query can render under a new query, and the model-side guard is reset to 0 by `setEntries`, defeating D-15 after any catalog refresh; (4) QML/test issues (status row overlaying usable results, timing-sensitive tests).

No security vulnerabilities found: no injection surface (SQL comes only from `GenerateSQLFromUserQuery`, reveal/launch arguments are quoted or passed as `lpFile` without shell parsing, no secrets, no unsafe deserialization).

## Warnings

### WR-01: Data race on the shared QSettings instance (worker thread vs UI thread)

**File:** `src/core/LaunchHistory.cpp:56-98` (reads), `src/core/LaunchHistory.cpp:39-54` (writes), wired at `src/app/main.cpp:88`

**Issue:** `LaunchHistory::m_settings` is a single QSettings instance accessed concurrently from two threads:
- **UI thread:** `recordLaunch()` / `addExecutable()` (called from the LaunchController default reporter on the UI thread after every successful launch).
- **Worker thread:** `trackedExecutables()` — the FileSearch worker lambda calls the `TrackedSource` seam (`[&history] { return history.trackedExecutables(); }`) from the QtConcurrent pool on every dispatch.

QSettings is not safe for concurrent use of one instance (the settings map / cache layers are unlocked member state). The race is reachable in normal use: press Enter (launch → `recordLaunch` → `setValue` + `sync`) while a file query is in flight (worker iterating `allKeys()`/`value()`), or add an executable while a query runs. Consequences: torn reads of the INI (launch counts lost or duplicated), or — since `%APPDATA%\TID\wisp\wisp.ini` also holds the hotkey config — cross-group corruption. D-10/D-11 persistence is the phase's own data; this race puts it at risk.

**Fix:** Guard the QSettings member with a `QMutex` in LaunchHistory (lock in `recordLaunch`, `addExecutable`, `trackedExecutables`, `launchCount`), or — matching the phase's own "worker captures copies by value, never touches UI-thread state" discipline — snapshot `trackedExecutables()` on the UI thread at `dispatch()` time in `FileSearch::dispatch()` and hand the snapshot (a `QVector<AppEntry>` value copy) to the worker lambda instead of the seam. The latter also removes the file I/O from the worker.

### WR-02: GetData row buffer: no length-slot use, status check only for NULL, buffer reuse across rows

**File:** `src/win/WinSearchQuery.cpp:343, 356-378`

**Issue:** The row buffer (`rowData`, 5296 bytes) is allocated once and **reused for every row** without clearing; the three length slots (which the bindings explicitly request via `DBPART_LENGTH`) are never read. Consequences:

1. **Error statuses fall through:** only `DBSTATUS_S_ISNULL` is skipped. If the provider reports any other non-OK status (e.g. `DBSTATUS_E_*`), the value slot was not written and the code reads **stale bytes from the previous row** → wrong path/name/folder rows that get displayed and launched.
2. **NUL-termination is assumed:** `QString::fromWCharArray(ptr)` (no length) and `_wcsicmp(..., L"true")` rely on an in-buffer NUL. Per OLE DB semantics the provider writes the terminator only if it fits within `cbMaxLen` — a path of ≥2048 chars (long-path support makes these reachable) fills the 4096-byte slot with no terminator, so `fromWCharArray` reads into the name/folder fields (garbage subtitle) and, in the worst case, past the 5296-byte allocation for the folder `_wcsicmp` — an out-of-bounds read. The index can contain long paths; the launch path derived from a garbage/truncated string then feeds `ShellExecuteEx`.

**Fix:** Check `status == DBSTATUS_S_OK` (skip anything else); use the length slots: `QString::fromWCharArray(ptr, int(len / sizeof(wchar_t)))` for path/name and a length-bounded compare for the folder column; treat `len >= cbMaxLen` as truncation (skip the row or clamp). This removes both the stale-bytes and OOB paths.

### WR-03: Generation guard validates ordering, not query text — stale rows can render under a new query

**File:** `src/core/ResultsModel.cpp:167-175` and `:44-46`, `src/core/FileSearch.cpp:145-157`

**Issue:** The D-15 generation counter proves *recency*, not *relevance*. Two gaps:

1. **Debounce-window merge of old text:** user types "abc" → gen N dispatched (worker slow) → clears, types "def" → model `m_query` is "def" instantly, but the gen-N dispatch happens only after the new 150ms debounce. The gen-N result (computed for "abc") arrives in that window: `FileSearch::onFinished` accepts it (`N == m_generation` — no newer dispatch exists yet), and `ResultsModel::setFileResults` merges it against the *current* `m_query` "def". Every "abc" file re-scores as a base-tier 100 row → up to 5 stale rows appear under "def" until the "def" query lands (~150ms+). This visibly contradicts success criterion 4 ("stale results never appear") and D-15's stated intent.
2. **Model-side guard reset:** `setEntries()` resets `m_fileGeneration = 0` while FileSearch's counter keeps climbing. After any catalog refresh (background `ensureFresh` → `refreshed` → `setEntries`), an in-flight result for the previous query text passes the model guard (`gen >= 0`) and is stored; it then merges into the next typed query — again stale-text rows.

**Fix:** Carry the query text with the generation end-to-end (`WorkerResult.query`, add `query` to `resultsReady`, compare against `m_query` in `setFileResults`; drop on mismatch). Additionally, never reset `m_fileGeneration` in `setEntries` (only clear `m_fileEntries`) so the model-side guard stays monotonic across refreshes.

### WR-04: Status row overlay covers usable app results

**File:** `qml/MainWindow.qml:283`

**Issue:** `statusRow` is visible whenever `resultsModel.query !== "" && !fileSearch.indexerOk` — regardless of whether the list actually contains app matches. With the indexer disabled/building and a query that matches apps, the centered status text is drawn **on top of the usable result rows** (the empty-state gate correctly checks `resultsView.count === 0`, but the status row does not). D-18 locates the status row "in the same space as 'No results for…'" — i.e., the *empty* space — and the phase's own key-decision claims "the overlay owns the space only when it must". As gated, it owns the space even when results are present, obscuring the very rows the user still can launch (checkpoint step "apps still working" was verified with the text floating over the list).

**Fix:** Gate on emptiness too: `visible: resultsModel.query !== "" && !fileSearch.indexerOk && resultsView.count === 0` — or anchor the status text to the top of the list area (below the separator) so it never overlaps rows.

### WR-05: Timing-sensitive test suites (flaky under CI load)

**File:** `tests/tst_filesearch.cpp:70-117` (esp. `staleGenerationDropped_D15`, `debounceFiresOnceAfterQuiet_D12`)

**Issue:** The tests use real sleeps (`QTest::qWait` 100/200/350ms) against the **global QtConcurrent thread pool**. `staleGenerationDropped_D15` depends on worker-1 (300ms `QThread::msleep` in the seam) *completing after* worker-2, but both run on the shared pool: on a 1-2 core CI machine (or with pool contention from other tests), worker-2 may be queued behind worker-1, so "b" completes first is no longer guaranteed and `results.count() == 1` fails spuriously. The tests assert *real-time ordering* of an async pipeline, which is exactly what the generation design was meant to make deterministic.

**Fix:** Inject a dedicated `QThreadPool` (with controlled thread count) into FileSearch for the test, or replace wall-clock waits with `QSignalSpy::wait()` on `resultsReady`/`stateChanged` plus generous timeouts, and assert on the *final* generation value rather than on completion counts mid-flight.

## Info

### IN-01: Provider-allocated HROW array would leak

**File:** `src/win/WinSearchQuery.cpp:345-347`

**Issue:** `GetNextRows(..., &hrowsPtr)` passes the address of a local copy of the vector's data pointer. OLE DB permits the provider to allocate its own HROW array and return it via `*prghRows`; the caller must then `CoTaskMemFree` it. The code never checks whether `hrowsPtr` still points at `hrows.data()`, so a provider that allocates would leak per fetch batch. The Windows Search provider writes in place (this is the WSOleDB sample pattern), so it is low risk.

**Fix:** After the call, `if (hrowsPtr != hrows.data()) { CoTaskMemFree(hrowsPtr); }` — or keep the pointer check and fall back to reading from `hrowsPtr` if replaced.

### IN-02: `trackedExecutables` loses the `isFolder` flag

**File:** `src/core/LaunchHistory.cpp:68-77`

**Issue:** Launched folders are recorded by `recordLaunch` (D-10 tracks every successful launch, folders included — reasonable), but `trackedExecutables()` reconstructs entries with `isFolder = false`. The folder then renders with the monogram initial instead of the ▸ glyph (D-04 contract) and loses the folder visual distinction. Launch/reveal still work (`ShellExecuteEx` on the path).

**Fix:** Set `e.isFolder = QFileInfo(path).isDir()` when reconstructing (cheap, derived like displayName).

### IN-03: `addExecutable` stores non-.exe paths

**File:** `src/core/LaunchHistory.cpp:48-54`, `src/app/main.cpp:89-92`

**Issue:** The native dialog filter `"Executables (*.exe)"` is advisory — the user can type a non-.exe name and the dialog accepts it, or the seam can be called with any path. Such an entry joins the catalog and later gets `ShellExecuteEx`'d with the open verb (opening a .txt/.docx with its default handler). This is user-intent-driven and not an injection, but it undermines the phase's .exe-only contract (D-09) and the "application launcher" scope.

**Fix:** In `LaunchHistory::addExecutable` (or the dialog seam), reject paths not ending in `.exe` (case-insensitive) — the same predicate the file pipeline already owns (`isAllowedResult`).

### IN-04: `revealInExplorer` launches `explorer.exe` by bare name

**File:** `src/win/WinLaunch.cpp:120`

**Issue:** `sei.lpFile = L"explorer.exe"` is resolved by the shell through its search order (which includes the process CWD — `lpDirectory` is nullptr). A hostile `explorer.exe` placed in the launcher's working directory could be picked up instead of the system binary. Very low practical risk (the launcher's CWD is wherever the exe lives), but the fix is free.

**Fix:** Resolve the full path once: `QDir::fromNativeSeparators(qEnvironmentVariable("WINDIR")) + "\\explorer.exe"` (or `SHGetKnownFolderPath(FOLDERID_Windows)`), falling back to the bare name.

---

## Verified Claims (no issues found)

- **Generation guards on both sides** — present and ordering-correct (FileSearch `onFinished` + `ResultsModel::setFileResults`); the gaps in WR-03 are text-relevance gaps, not missing guards.
- **5-file cap (D-03)** — enforced structurally after the union sort; apps never dropped; test-proven.
- **COM thread affinity** — `CoInitializeEx(COINIT_MULTITHREADED)` per call on the worker, S_FALSE/RPC_E_CHANGED_MODE tolerated, CoUninitialize only when this call initialized; 10-interface chain fully RAII-released on every early-return path.
- **Debounce (D-12)** — correct single-shot restart, last text wins, empty-query bypass (D-14).
- **QSettings INI persistence** — path keys native-normalized (no group-separator corruption), displayName never stored, `sync()` after writes, test round-trips through QTemporaryDir.
- **ShellExecuteEx usage** — no elevation verb for file/folder rows (grep-verified, D-05 provably unreachable), user-cancelled UAC classified quiet, `SEE_MASK_FLAG_NO_UI` throughout.
- **SQL construction** — only `GenerateSQLFromUserQuery` (AQS escaping owned by the helper); the WHERE fragment is a compile-time constant.
- **Test fakes** — genuinely exercise the seams (QueryFn/StatusFn/TrackedSource/AddExeDialog/AddEntryStore/Revealer); zero OS calls in CI.

---

_Reviewed: 2026-08-10_
_Reviewer: OpenCode (gsd-code-reviewer)_
_Depth: standard_
