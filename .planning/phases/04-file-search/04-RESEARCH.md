---
phase: 04
slug: file-search
status: complete
researched: 2026-08-10
---

# Phase 4 Research — File Search (Windows Search COM pipeline)

> Phase-scoped research for "files join the search": Windows Search index queries on worker threads (LAUN-02), default-app open + containing-folder reveal (LAUN-03), debounce + generation counter + degraded-state UX. Supplements `research/STACK.md`, `research/ARCHITECTURE.md`, `research/PITFALLS.md` with the resolved OLE DB spike, the indexer-status mapping, and the filter strategy this phase locks. Verified against Microsoft Learn + Windows-classic-samples on 2026-08-10.

## Verified Facts (confidence HIGH unless noted)

### 1. OLE DB row consumption in C++ — SPIKE RESOLVED: raw OLE DB COM (no ATL, no ADO)
- **The deferred spike from STATE.md ("ATL CDataSource/CCommand vs ADO — MS sample is C#") is resolved by constraint:** the ATL consumer headers (`atldbcli.h`, `atldbcli` + ATL runtime) are **NOT installed** on this machine (verified: no `atlmfc/include/atldbcli.h` under any MSVC 2022 root, x86 or x64). Requiring ATL would force a Visual Studio installer change — a new build dependency the project forbids by convention (STACK "What NOT to Use": no extra deps without need).
- **ADO (`#import msado15.dll`)**: rejected — legacy COM interop, import-tlb noise, and the same rowset plumbing with a worse type story.
- **Chosen path — raw OLE DB COM via Windows SDK headers only** (`<oledb.h>`, `<msdasc.h>`), following the documented interface sequence (mirrors the ATL WSOleDB sample flow in `Windows-classic-samples/Win7Samples/winui/WindowsSearch/WSOleDB`, re-implemented on raw interfaces):
  1. `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` on the worker thread (same MTA discipline as the 03-03 catalog worker; S_FALSE/RPC_E_CHANGED_MODE reuse tolerated).
  2. `CLSIDFromProgID(L"Search.CollatorDSO.1", &clsid)` → `CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDBInitialize, ...)`.
  3. QI `IDBProperties`; set `DBPROP_INIT_DATASOURCE` = connection string from `ISearchQueryHelper::get_ConnectionString()` (with the `provider=` token stripped — it names this same provider) and `DBPROP_INIT_PROMPT` = `DBPROMPT_NOPROMPT`; `IDBInitialize::Initialize()`.
  4. QI `IDBCreateSession` → `CreateSession(nullptr, IID_IDBCreateCommand)` → `IDBCreateCommand::CreateCommand(nullptr, IID_ICommandText)` → `ICommandText::SetCommandText(DBGUID_DEFAULT, sqlW)` (SQL from `GenerateSQLFromUserQuery`).
  5. QI `ICommand` → `ICommand::Execute(nullptr, IID_IRowset, nullptr, nullptr, &pRowset)`.
  6. `IColumnsInfo::GetColumnInfo` → resolve ordinals for `System.ItemPathDisplay`, `System.ItemNameDisplay`, `System.IsFolder` (one-time per query, bind-on-demand).
  7. `IAccessor::CreateAccessor(DBACCESSOR_ROWDATA, ...)` with a small `DBBINDING` array for the columns above → `IRowset::GetNextRows` + `IRowset::GetData` loop → build `QVector<FileResult>` → release everything (per-query, `CoUninitialize` at batch end).
- **Column contract:** `System.ItemPathDisplay` (e.g. `C:\Users\me\Downloads\setup.exe` — subtitle + launch path + explorer reveal path in one), `System.ItemNameDisplay` (title), `System.IsFolder` (boolean — D-04 folder rows). `System.ItemUrl` (`file:///...`) NOT needed — ItemPathDisplay is already a local path.
- **Fresh `ISearchQueryHelper` per query** (STACK locked; PITFALLS #5 — cached helpers go stale as the catalog evolves): `CoCreateInstance(__uuidof(CSearchManager))` → `GetCatalog(L"SystemIndex")` → `GetQueryHelper()` → `put_QuerySelectColumns(L"System.ItemPathDisplay, System.ItemNameDisplay, System.IsFolder")` → `put_QueryMaxResults(30)` (PITFALLS #5: default is unlimited) → `put_QuerySyntax(SEARCH_ADVANCED_QUERY_SYNTAX)` → `put_QueryTermExpansion(SEARCH_TERM_PREFIX_ALL)` (default — prefix expansion) → `GenerateSQLFromUserQuery(queryW, &sqlW)` → `CoTaskMemFree(sqlW)`. **Never string-concatenate user input into SQL** — the helper escapes (PITFALLS #5).
- **Locale caveat (PITFALLS #5):** `GenerateSQLFromUserQuery` uses regional locale settings — acceptable for a launcher (keyword-only queries); do NOT set `put_QueryContentLocale`/`put_QueryKeywordLocale` (leave defaults) to avoid mismatched date parsing; we never query dates.
- **Worker-thread only** (STACK + PITFALLS #5: OLE DB `Execute` blocks — multi-second freezes on the UI thread). All COM objects created AND used on the same worker thread (PITFALLS #3 discipline, AppCatalog precedent). Risk note: the Search.CollatorDSO provider is used in MTA by community implementations; if the provider misbehaves in MTA on some builds, the fallback is an STA worker (`CoInitializeEx(COINIT_APARTMENTTHREADED)`) — same interface, one-line change. MEDIUM confidence, escape hatch documented.

### 2. Indexer status → D-17's three trouble states (on-query, D-16)
- `ISearchCatalogManager::GetCatalogStatus(CatalogStatus *pStatus, CatalogPausedReason *pPausedReason)` — the sanctioned status check (PITFALLS #5 references exactly this). Enum (searchapi.h):
  | Value | Name | Queries OK? |
  |---|---|---|
  | 0 | `CATALOG_STATUS_IDLE` | yes (index current) |
  | 1 | `CATALOG_STATUS_PAUSED` | yes (user-paused or back-off; queries still process) |
  | 2 | `CATALOG_STATUS_RECOVERING` | yes (queries + indexing both process) |
  | 3 | `CATALOG_STATUS_FULL_CRAWL` | yes (indexing runs, queries still work) |
  | 4 | `CATALOG_STATUS_INCREMENTAL_CRAWL` | yes |
  | 5 | `CATALOG_STATUS_PROCESSING_NOTIFICATIONS` | yes |
  | 6 | `CATALOG_STATUS_SHUTTING_DOWN` | **no** — cannot be queried |
- **Mapping to D-17 states (locked):**
  - **Disabled/stopped:** `CoCreateInstance(CSearchManager)` fails OR `GetCatalog` fails (service not running — e.g. `0x80070422` service-not-started surface, or `E_FAIL`). → message: "Indexing is turned off — enable Windows Search to find files"
  - **Building:** `GetCatalogStatus` ∈ {FULL_CRAWL, INCREMENTAL_CRAWL, PROCESSING_NOTIFICATIONS, RECOVERING} → message: "Windows is still building its search index — files will appear soon"
  - **Unavailable:** `GetCatalogStatus` == SHUTTING_DOWN OR catalog OK but `ICommand::Execute`/row iteration fails → message: "File search is unavailable right now"
  - `IDLE`/`PAUSED` → OK (queries run; PAUSED still answers queries — NOT a trouble state).
- **On-query laziness (D-16):** the status check happens on the worker when a file query fires — zero cost when idle, no polling (CONTEXT D-16 locked). Disabled/Unavailable → skip the query, emit status only. Building → run the query (partial results are fine) AND emit the Building status (D-18 status row shows alongside).

### 3. .exe + folder filter (D-04/D-09) — WHERE restriction + post-filter belt-and-braces
- **WHERE restriction** via `put_QueryWhereRestrictions` (SQL fragment, ANDed by the helper): `System.ItemUrl LIKE 'file:%' AND (System.FileExtension='.exe' OR System.IsFolder=TRUE)` — `file:%` scoping is the PITFALLS #5-recommended scope restriction; the extension/folder clause implements D-09 at the source (index does the filtering, smaller rowset, less junk).
- **Post-filter (correctness gate):** after row fetch, keep rows where `System.IsFolder == TRUE` OR `System.ItemPathDisplay` ends with `.exe` (case-insensitive) — catches any provider/SQL-dialect drift. Both layers are cheap; the post-filter is authoritative (D-09 "post-query filter" option).
- **Cap ~5 file rows (D-03):** enforced at the model merge (see §6), not the query — the query fetches up to 30, the model keeps ≤5 file rows above apps.
- **Search scope (D-06):** default SystemIndex scope — profile + libraries, whatever the index covers. NO explicit install-root scoping. The tracked/added .exe catalog (§4) is the second source.

### 4. Launch tracking + manual add (D-10/D-11) — QSettings INI pattern
- **INI pattern (existing):** `QSettings(QSettings::IniFormat, QSettings::UserScope, "TID", "wisp")` → `%APPDATA%\TID\wisp\wisp.ini` (HotkeyManager precedent, HotkeyManager.cpp:84-87). Launch tracking reuses it: group `launchHistory`, keys = `targetPath`, values = `count` (int) + `name` (displayName) — or one key per path with count in the value; exact format is planner discretion (CONTEXT grants it) but MUST round-trip through QSettings IniFormat.
- **Record on launch (D-10):** LaunchController's `Launched` outcome increments the counter for the launched entry (injectable `LaunchHistory` seam for tests, mirroring the Launcher/ResultReporter DI pattern). Files AND classic apps both tracked (all wisp launches — that's the "frequently-launched" signal).
- **Tracked catalog (D-10/D-06):** `LaunchHistory::trackedExecutables()` → `QVector<AppEntry>` (Source::File, targetPath = path, displayName = filename) — the second search source that covers unindexed volumes (WoW.exe example). Manual add (D-11) writes the same store → picked .exe appears immediately.
- **No registry scraping** (D-10 locked: no App Paths, no MuiCache/UserAssist — STACK/03 precedent).

### 5. Launch & reveal (LAUN-02/LAUN-03, D-04/D-05)
- **Enter on file** = `ShellExecuteExW` verb `open` on the file path → default app association launches it. **WinLaunch::launchClassic already implements exactly this** (open verb, `SEE_MASK_FLAG_NO_UI`, target-parent `lpDirectory`, ERROR_CANCELLED/SE_ERR_ACCESSDENIED → CancelledByUser) — file entries reuse it with `targetPath` = file path, `elevated=false` (WinLaunch.h contract, verified 03-04).
- **Enter on folder** = same `open` verb on the folder path → Explorer opens the folder (D-04).
- **Ctrl+Enter on file/folder** = reveal in Explorer: `ShellExecuteExW(nullptr, L"open", L"explorer.exe", L"/select,\"" + path + L"\"", nullptr, SW_SHOWNORMAL)` — the documented technique; **quote the path inside the argument** (paths with spaces break `/select,` otherwise). New `WinLaunch::revealInExplorer(const QString &path)` firewall function (WinLaunch family addition, same LaunchResult classification).
- **Ctrl+Shift+Enter on file/folder (D-05)** = SILENTLY NORMAL launch: the controller maps any elevated request for Source::File / folder rows to `elevated=false` (no hint, no refusal UI — D-05 locked; runas makes no sense for file launches).

### 6. Merge & ranking (D-01/D-02/D-07) — model-side, score decides
- **One interleaved ranked list** (D-01): ResultsModel keeps the existing app pipeline (instant per-keystroke filter on m_entries) and gains a **file result set** merged into the same `m_order` by score desc, then displayName asc (D-05 tie-break). No sectioning, no app-priority.
- **File score:** `FuzzyMatcher::score(displayName, query).score` (same ranker, consistent ladder — files that filename-match rank by the same tiers). **Path-only matches** (query matched the path, not the name — D-07 "tax 2025" folder case) get a base score constant `kPathMatchScore = 100` (below the 400 subsequence tier → path matches appear, but never above name matches). Named constant in the model, documented.
- **Cap:** after ranking, if file rows > 5 (D-03), drop the lowest-scored file rows — apps are never dropped.
- **Subtitle (D-02):** for Source::File rows, SubtitleRole = full path (`targetPath`/ItemPathDisplay), NOT the QFileInfo baseName the Lnk path uses. Filename is the DisplayNameRole. `MatchRangesRole` still computed (Phase-5 highlight contract holds for file rows).
- **New role:** `IsFolderRole` (bool) → QML folder glyph in the monogram (D-04, glyph swaps to icons in Phase 5).

### 7. Debounce / generation / quiet fill-in (D-12/D-13/D-14/D-15)
- **Fixed 150ms debounce** (D-12, roadmap-mandated range 120-150ms): `QTimer::singleShot` style restart on each query change; apps re-filter instantly (existing 03 pipeline untouched), only the file query waits for typing to pause.
- **Generation counter (D-15):** `quint64 m_generation` incremented on every dispatched file query; results carry the generation; the UI-thread completion drops results whose generation != latest (stale-drop, roadmap criterion 4). Enforced in BOTH FileSearch (dispatch/completion) and ResultsModel (`setFileResults(gen, entries)` stores latest gen, drops older) — defense in depth, model-side is unit-testable without the worker.
- **Empty query → apps only (D-14):** file pipeline bypasses when query is empty (no dispatch, no status check). Quiet fill-in (D-13): no spinner, no indicator — file rows simply appear when the query lands. **No new UI indicator types this phase.**
- **Threading:** QtConcurrent::run + QFutureWatcher on the UI thread (AppCatalog 03-03 pattern — worker lambda owns COM init/cleanup, dedupe/sort/swap on the UI thread).

### 8. Manual "Add executable…" (D-11)
- **Pinned result-row action** in the results list (below the ListView, inside the surface) → C++ `Q_INVOKABLE` on the file-search context object → `QFileDialog::getOpenFileName(nullptr, ..., filter "Executables (*.exe)")` (app links Qt Widgets already — QApplication + QFileDialog native on Windows) → add to LaunchHistory store (manual entries get a distinct marker so they are never pruned) → re-emit tracked catalog → immediate availability. Tray-drop and settings-window placement rejected (D-11 locked).

### 9. Degraded-state UX (D-17/D-18)
- **Status row** = non-selectable row in the list area (same space as "No results for…" empty state), Theme-token-only, shown while query non-empty AND file search troubled; apps keep working; no modal, no transient hint (D-18 locked). Copy (planner verbatim-copy discipline, 03 empty-state style):
  - Disabled: **"Indexing is turned off — enable Windows Search to find files"**
  - Building: **"Windows is still building its search index — files will appear soon"**
  - Unavailable: **"File search is unavailable right now"**
- The status row must never take focus/selection (non-selectable — no model row, a QML overlay in the list area; the model never emits it).

## Validation Architecture

- **Framework:** Qt Test (Qt6::Test) — existing harness (10 green suites). New targets: `tst_search` (plan 01), `tst_filesearch` (plan 02), `tst_history` (plan 03), plus extensions to `tst_model` and `tst_launch` (plan 03).
- **Mapping (plan → what's proven):**
  - `tst_search` (04-01): pure helpers only — indexer-status → state mapping (all 7 enum values incl. PAUSED-not-troubled), `.exe`/folder post-filter predicate (case-insensitive, folder-kept), WHERE-restriction string content gate (contains `file:%`, `FileExtension='.exe'`, `IsFolder=TRUE`). The live OLE DB row iteration is dev-machine manual verification (needs a real index).
  - `tst_filesearch` (04-02): injected fake query fn + fake tracked-source — debounce fires ~150ms after the last change (QTest::qWait), generation drop (stale results never delivered), empty-query bypass (no dispatch), 5-cap preserved to the model, status propagation (Disabled skips query, Building queries + status), quiet fill-in (no extra signals).
  - `tst_model` additions (04-03): setFileResults merge — interleaved score order (D-01), cap 5 file rows (D-03), path-only base score tier (D-07), subtitle = full path for File source (D-02), stale-generation drop (D-15), selection clamp after merge.
  - `tst_launch` additions (04-03): file entry Enter → non-elevated launchClassic path; elevated request on file/folder → silently normal (D-05, zero signals); revealInExplorer called on Ctrl+Enter with quoted path (D-03/LAUN-03); folder Enter → open verb. All with injected fakes — no OS calls.
  - `tst_history` (04-03): QSettings-in-TESTDATA-dir round-trip (record → reload), count increments, manual-add marker survives reload, trackedExecutables() shape (Source::File).
- **Manual-only verifications (04-VALIDATION.md):** live Windows Search query on the dev machine (type an .exe name → file row), real Enter launch, real Ctrl+Enter Explorer reveal with file selected, real folder open, real "Add executable…" flow, indexer-building state (only reachable on a fresh/clean VM — Phase-6 territory; validate the Disabled state by stopping the Windows Search service on the dev machine: `Stop-Service WSearch` → status row appears), typing-stays-smooth feel check. COM/index tests need a live desktop session — never CI.
- **Quick run:** `ctest --test-dir build/dev -R "tst_search|tst_filesearch|tst_history|tst_model|tst_launch" --output-on-failure`. Full: `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure`.
- **Windows note:** tests never touch a real index (fixtures + injected fakes); `Stop-Service WSearch` manual validation must be reverted (`Start-Service WSearch`) by the verifier.

## Decisions This Phase Imposes (locked, mirror into plans)

1. **D-01:** File results merge with apps in ONE interleaved ranked list — score decides order, no sectioning.
2. **D-02:** File row = filename title + full path subtitle (elided), monogram = file initial (44px ResultsRow pattern).
3. **D-03:** ~5 file results cap per query; apps dominant.
4. **D-04:** Folders appear with a distinct text glyph in the monogram; Enter opens the folder in Explorer.
5. **D-05:** Ctrl+Shift+Enter on file/folder = silently normal launch (no hint, no refusal).
6. **D-06:** Scope = SystemIndex default + tracked/added .exe catalog. No install-root scoping.
7. **D-07:** Query matches filename AND path (path-only matches get base score 100, below name matches).
8. **D-08:** Plain keywords with AQS escaping (`GenerateSQLFromUserQuery`); no AQS tutorial UI.
9. **D-09:** ONLY .exe + folders from the index (WHERE restriction + post-filter).
10. **D-10:** Frequently-launched discovery = wisp's own launches recorded to INI + manual add. No registry parsing.
11. **D-11:** "Add executable…" pinned result-row action → native file dialog → joins catalog.
12. **D-12:** Fixed ~150ms debounce on the file query; apps re-filter instantly.
13. **D-13:** Quiet fill-in — no spinner, no indicator row.
14. **D-14:** Empty query = apps only; file search engages only when query non-empty.
15. **D-15:** Generation counter drops stale results (both worker and model).
16. **D-16:** Indexer trouble detected on-query, lazily, on the worker.
17. **D-17:** 3 distinct trouble states with locked messages (Disabled / Building / Unavailable).
18. **D-18:** Non-selectable status row in the list area; apps keep working.

## Remaining Unknowns (accepted, not blockers)
- Search.CollatorDSO MTA-vs-STA behavior on some builds — documented escape hatch (STA worker, one-line change).
- Exact behavior of `ISearchQueryHelper::put_QueryWhereRestrictions` SQL fragment with `System.IsFolder=TRUE` on older Win10 indexer builds — the post-filter is the authoritative gate; restriction drift degrades to "more rows fetched", never wrong results.
- Live OLE DB row iteration quality (column types, WSTR vs variant) — the plan's Wave-1 task includes the raw-interface spike in-task with the WSOleDB sample as reference; manual dev-machine verification covers the rest.
