---
phase: 04-file-search
plan: 01
subsystem: windows-integration
tags: [windows-search, ole-db, com, sql, qt, cpp]

# Dependency graph
requires:
  - phase: 02-app-index
    provides: firewall pattern (namespace functions, per-call COM objects), AppEntry struct
  - phase: 03-hotkey
    provides: COM-discipline precedent (CoInitializeEx MTA per batch, S_FALSE/RPC_E_CHANGED_MODE tolerance)
provides:
  - WinSearchQuery firewall: queryFiles (raw OLE DB pipeline), checkIndexStatus (D-16 probe)
  - Pure helpers classifyCatalogStatus / isAllowedResult / buildWhereRestriction (unit-tested)
  - AppEntry::Source::File + isFolder field (downstream contract)
  - Live-verified OLE DB row-consumption recipe (16/8-aligned bindings, WSTR IsFolder, AND-prefixed restriction, CLSCTX fallback)
affects: [04-02, 04-03, 04-04, 04-05, 05-icon, launcher]

# Tech tracking
tech-stack:
  added: [raw OLE DB COM via SDK headers (oledb.h, msdasc.h, searchapi.h) - no ATL/ADO]
  patterns: [ComPtr RAII template (10-interface chain, release-every-path), per-query fresh ISearchQueryHelper, DBBINDING slot alignment rule, D-09 post-filter gate]

key-files:
  created: [src/win/WinSearchQuery.cpp, src/win/WinSearchQuery.h, tests/tst_search.cpp]
  modified: [src/core/AppEntry.h, CMakeLists.txt]

key-decisions:
  - "CSearchManager must be created with CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER - Win11 registers it out-of-process only (empty InprocServer32 stub); CLSID_CSearchManager external symbol does not link - use __uuidof(CSearchManager)"
  - "System.IsFolder binds as DBTYPE_WSTR ('True'/'False'), not DBTYPE_BOOL - provider rejects BOOL with BADBINDINFO"
  - "DBBINDING value/length slots must be 16-byte aligned, status slots 8-byte aligned - compact 4-aligned layout rejected with BADBINDINFO"
  - "'AND ' prefix required on put_QueryWhereRestrictions fragment (provider appends verbatim after CONTAINS) - kept out of the locked contract string"

patterns-established:
  - "Firewall pattern: namespace free functions, no classes, qWarning never crash, return {} on failure"
  - "OLE DB row consumption: GetColumnInfo -> resolve ordinals by name -> CreateAccessor -> GetNextRows/GetData -> ReleaseRows"
  - "Post-filter gate as authoritative filter (isAllowedResult) even though WHERE clause pre-filters at source"

requirements-completed: [LAUN-02]

# Metrics
duration: 40min
completed: 2026-08-10
---

# Phase [4] Plan [01]: WinSearchQuery Firewall Summary

**Raw OLE DB COM pipeline (SDK headers only) that turns a user query into .exe/folder rows from the Windows Search index - with a live-verified row-consumption recipe (16/8-aligned DBBINDING slots, WSTR IsFolder, AND-prefixed restriction, out-of-process-safe CLSCTX), an on-query indexer-status probe, and the AppEntry::File contract locked for every downstream plan**

## Performance

- **Duration:** 40 min
- **Started:** 2026-08-10 03:45Z
- **Completed:** 2026-08-10 04:04Z
- **Tasks:** 2 (both TDD)
- **Files modified:** 5

## Accomplishments
- WinSearchQuery firewall implemented and verified live: `queryFiles` (raw OLE DB chain: CollatorDSO.1 -> IDBInitialize -> IDBCreateSession -> IDBCreateCommand -> ICommandText -> IRowset -> IAccessor) returns real index rows - smoke run on the dev machine produced 2 notepad.exe hits and 30 folder rows for "C:\Program Files", all post-filtered to .exe/folders, capped at 30
- `checkIndexStatus` (D-16 probe) live-verified: reports Ok on this machine (indexer idle)
- Three pure helpers unit-tested (3/3 suites): classifyCatalogStatus covers all 7 CatalogStatus values, isAllowedResult exact (.exe case-insensitive + folders), buildWhereRestriction exact locked string
- **Spike resolution by constraint:** the plan's flagged unknown (row consumption: ATL vs ADO) settled by empirical diag - raw COM + verified binding layout works
- AppEntry::Source::File + isFolder committed - the phase-4 contract 04-02..04-05 consume

## task Commits

Each task was committed atomically:

1. **task 1: Extend AppEntry + declare the WinSearchQuery firewall contract** - `971be1a` (feat)
2. **task 2: Implement the raw OLE DB COM pipeline + tst_search (RED-first)** - `c36a829` (test), `aa9a2a3` (feat)

**Plan metadata:** pending final docs commit (.planning metadata)

_Note: TDD task 2 has two commits (test -> feat); no refactor needed_

## Files Created/Modified
- `src/win/WinSearchQuery.cpp` - Raw OLE DB COM pipeline: queryFiles, checkIndexStatus, pure helpers; ComPtr RAII; makeBstr (CoTaskMemAlloc BSTR, no oleaut32 link); 16/8-aligned RowLayout; D-09 post-filter
- `src/win/WinSearchQuery.h` - Firewall contract: FileResult, IndexerState, queryFiles/checkIndexStatus/classifyCatalogStatus/isAllowedResult/buildWhereRestriction
- `tests/tst_search.cpp` - 3 behavior suites (status mapping, .exe/folder predicate, WHERE string)
- `src/core/AppEntry.h` - Source::File + isFolder field added
- `CMakeLists.txt` - wisp_core gains WinSearchQuery.cpp; tst_search target in BUILD_TESTING

## Decisions Made
- **CLSCTX fallback for CSearchManager** - `CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER`; INPROC-only fails REGDB_E_CLASSNOTREG on this Win11 machine (verified: HKCR InprocServer32 for CSearchManager is an empty stub; the server is registered out-of-process). Also: `CLSID_CSearchManager` external symbol does not link against ole32/uuid - `__uuidof(CSearchManager)` used (same GUID).
- **IsFolder as DBTYPE_WSTR** - provider exposes it as string column ("True"/"False"); DBTYPE_BOOL binding -> BADBINDINFO. Parsed with `_wcsicmp(..., L"true")`.
- **16/8-aligned DBBINDING slots** - value/length 16-aligned, status 8-aligned; the compact 4-aligned WSOleDB-sample layout is rejected (BADBINDINFO on every binding). Verified via 12 diag iterations; production layout locked in RowLayout.
- **"AND " prefix at the call site** - `L"AND " + buildWhereRestriction()`; the helper appends the fragment verbatim after the CONTAINS clause, so the fragment must begin with AND (0x80040e14 otherwise). Contract string stays AND-less per T-04-01 grep gate.
- **No oleaut32.lib** - BSTR built manually (DWORD prefix + data + NUL) with CoTaskMemAlloc, honoring the plan's "no new link libs" constraint.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] CSearchManager CoCreateInstance fails under CLSCTX_INPROC_SERVER**
- **Found during:** task 2 (live smoke; plan assumed INPROC registration per STACK)
- **Issue:** `CoCreateInstance(CSearchManager, ..., CLSCTX_INPROC_SERVER)` -> 0x80040154 REGDB_E_CLASSNOTREG on the dev machine. Diag proved CSearchManager is registered out-of-process only (empty HKCR InprocServer32 stub); `CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER` succeeds.
- **Fix:** Combined CLSCTX in both queryFiles and checkIndexStatus, with a comment documenting the machine-verified behavior.
- **Files modified:** src/win/WinSearchQuery.cpp
- **Verification:** smoke.exe reports `indexer state: 0 (Ok)`; diag harness showed CLSCTX_LOCAL_SERVER/ALL succeed, INPROC-only fails.
- **Committed in:** aa9a2a3

**2. [Rule 1 - Bug] put_QueryWhereRestrictions fragment missing leading "AND "**
- **Found during:** task 2 (live verification)
- **Issue:** Provider fails ICommand::Execute with 0x80040e14 DB_E_ERRORSINCOMMAND when the restriction fragment doesn't begin with AND - the helper generates `... WHERE CONTAINS(...) <fragment>` verbatim.
- **Fix:** Prepended `L"AND "` at the call site only; buildWhereRestriction() keeps the locked AND-less string (test contract and T-04-01 grep gate intact).
- **Files modified:** src/win/WinSearchQuery.cpp
- **Verification:** SQL output valid (`WHERE CONTAINS(...) AND System.ItemUrl LIKE ...`), Execute succeeds, rows returned.
- **Committed in:** aa9a2a3

**3. [Rule 1 - Bug] System.IsFolder rejects DBTYPE_BOOL binding**
- **Found during:** task 2 (live verification)
- **Issue:** CreateAccessor -> 0x80040e21 with DBBINDSTATUS_BADBINDINFO on the folder binding; provider's native column type is WSTR (verified wType=12) and it does not convert.
- **Fix:** Bind folder as DBTYPE_WSTR (kFolderMax=128), parse with `_wcsicmp(..., L"true")` - provider returns "True"/"False".
- **Files modified:** src/win/WinSearchQuery.cpp
- **Verification:** CreateAccessor succeeds; rows show folder='True'/'False'; post-filter classification matches.
- **Committed in:** aa9a2a3

**4. [Rule 1 - Bug] Compact 4-aligned DBBINDING layout rejected (BADBINDINFO on all bindings)**
- **Found during:** task 2 (live verification)
- **Issue:** The WSOleDB-sample compact layout (value | ULONG len | ULONG status, all 4-aligned) fails CreateAccessor with 0x80040e21 and BADBINDINFO(3) on EVERY binding - even with correct types and sizes (diag9/diag7 proved sizes irrelevant).
- **Fix:** Bisected via 12 throwaway diag programs: provider requires value/length offsets 16-byte aligned and status offsets 8-byte aligned. RowLayout rewritten to the verified-good geometry (path 0/4096/4104, name 4112/5136/5144, folder 5152/5280/5288, rowSize 5296), each formula annotated with its alignment rationale.
- **Files modified:** src/win/WinSearchQuery.cpp
- **Verification:** CreateAccessor hr=0x00000000 st=0/0/0 across three candidate layouts; full end-to-end fetch (diag12) returned 30 rows with correct folder='True'/'False' values.
- **Committed in:** aa9a2a3

**5. [Rule 3 - Blocking] ATL headers absent on toolchain (plan-flagged unknown, resolved by constraint)**
- **Found during:** task 2 (implementation)
- **Issue:** The plan flagged the row-consumption spike (ATL vs ADO) as unknown. ATL headers verified absent on this machine -> raw COM via SDK headers, as the plan's fallback constraint dictated.
- **Fix:** None needed beyond the plan's own constraint resolution; implemented raw OLE DB with a minimal ComPtr RAII template (no ATL dependency).
- **Files modified:** src/win/WinSearchQuery.cpp
- **Verification:** Full pipeline works live (smoke run).
- **Committed in:** aa9a2a3

---

**Total deviations:** 5 auto-fixed (4 Rule 1 bugs, 1 Rule 3 blocking)
**Impact on plan:** All auto-fixes necessary for live correctness on this machine; no scope creep. The locked contracts (header exports, WHERE string, test behavior) all kept exactly as planned.

## Issues Encountered
- The OLE DB row-consumption spike consumed ~12 diagnostic iterations (diag.cpp through diag12.cpp, all throwaway, kept outside the repo in temp). Root causes were three independent provider quirks: CLSCTX registration, AND-prefix requirement, and binding-slot alignment. Each was bisected in isolation to avoid conflating them.
- qWarning output is not visible on smoke.exe stderr when no debugger is attached (Windows Qt message handler goes to OutputDebugString) - diag programs used printf for failure attribution instead.
- Smoke link initially failed with LNK2038/LNK2019 - the compile command was missing `/MDd` (debug runtime, matching debug Qt libs).

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- WinSearchQuery firewall (queryFiles/checkIndexStatus) ready for 04-02 (search coordinator) to call from a worker thread; FileResult contract (path/displayName/isFolder) ready for 04-03 (results UI), 04-04 (launch/reveal), 04-05 (verification)
- AppEntry::Source::File + isFolder ready for the model layer
- Live verification table in 04-VALIDATION.md can now be filled by the verifier with concrete evidence (smoke output above)
- Known caveat for downstream plans: 'calc' style System32 apps are NOT in the default Windows Search index (indexer covers user libraries) - file results complement, not replace, the Start Menu/UWP indexers

## TDD Gate Compliance
- RED gate: `c36a829` test(04-01) - tst_search failing on missing helpers (LNK2019), committed before implementation
- GREEN gate: `aa9a2a3` feat(04-01) - implementation, committed after tst_search passed
- REFACTOR gate: not needed (no cleanup required)
- Both required gate commits present in git log; sequence correct (test before feat)

## Self-Check: PASSED
- Files verified: src/win/WinSearchQuery.cpp, src/win/WinSearchQuery.h, tests/tst_search.cpp, src/core/AppEntry.h, 04-01-SUMMARY.md
- Commits verified: 971be1a (task 1), c36a829 (RED), aa9a2a3 (GREEN)

---
*Phase: 04-file-search*
*Completed: 2026-08-10*
