# Phase 4: File Search - Context

**Gathered:** 2026-08-10
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 4 adds file search to the working vertical slice: as the user types, a worker-thread Windows Search query runs against the SystemIndex (debounced ~150ms, generation-countered) and .exe file results merge into the existing results list. Enter launches an .exe with its default app (ShellExecuteEx), Ctrl+Enter opens the containing folder in Explorer with the file selected, and indexer trouble (disabled / building / error) shows a distinct friendly status row instead of a blank dead-end.

**USER SCOPE DECISION (2026-08-10):** File search is **.exe-only** — the user wants an application launcher, not a document finder: *"I want it to be an application launcher, so just executable apps"* / *"only .exe files for now"*. Broader file types (documents/media) are explicitly deferred.

Second source beyond the index: **frequently-launched apps that the index doesn't cover** (user example: WoW.exe on an unindexed SSD). wisp discovers these by (a) tracking every launch wisp itself performs, and (b) a manual "Add executable…" action the user can invoke from the results list.

**Success criteria (ROADMAP, adapted):** (1) typing a filename yields .exe file results merged with app results; (2) Enter opens the .exe; (3) Ctrl+Enter opens the containing folder in Explorer with the file selected; (4) typing never freezes or stutters — debounce ~150ms + generation counter + worker-thread query, stale results never appear; (5) indexer disabled/building/unavailable shows a distinct friendly message — never a blank dead-end.

**Requirement:** LAUN-02, LAUN-03. **Not in scope:** non-.exe file search (deferred by user), icons + match-highlight visuals (Phase 5), recency ranking (v2 LAUN-07/08), settings window (Phase 6), backdrop blur (v2).

</domain>

<decisions>
## Implementation Decisions

### Result Mix & Row Design
- **D-01:** File results merge with app results in ONE interleaved ranked list — score decides order (Rofi/Spotlight feel). No sectioning, no app-priority.
- **D-02:** File row shows **filename as title, full path as subtitle** (elided), monogram = file initial — reuses the existing 44px ResultsRow delegate pattern.
- **D-03:** **~5 file results cap** per query. Apps stay dominant in the 7-row visible window.
- **D-04:** Folders DO appear in file results, rendered with a **distinct text glyph in the monogram** (no image pipeline until Phase 5; glyph swaps cleanly to real icons later). Enter on a folder opens it in Explorer.
- **D-05:** Ctrl+Shift+Enter on a file/folder result = **silently normal launch** (elevation only applies to classic apps; no hint, no refusal UI).

### Search Scope & Semantics
- **D-06:** Search scope = **SystemIndex default scope** (profile + libraries — whatever the index actually covers) PLUS the frequently-launched/added .exe catalog. No explicit install-root scoping, no conditional query classification.
- **D-07:** The file query matches **filename AND path** (users type 'tax 2025' meaning a folder; path matches catch that).
- **D-08:** Query interpretation = **plain keywords with AQS escaping** (`GenerateSQLFromUserQuery` handles escaping; users don't learn AQS, but typing `kind:` etc. still works). No forced filename-only semantics, no AQS tutorial UI.
- **D-09:** **ONLY .exe files** returned from the index (`System.ItemType` / extension filter `ext:.exe`-style restriction or post-query filter). Folders may appear per D-04. Broader types deferred.
- **D-10:** Frequently-launched discovery = **track wisp's own launches only** (LaunchController records {name, targetPath, count} to the INI as launches happen) + manual registration. NO registry parsing (no App Paths, no MuiCache/UserAssist — STACK/03 avoided registry hacks for a reason; shell-history scraping is privacy-sensitive).
- **D-11:** Manual add UI = **"Add executable…" pinned result-row action** (visible in the results list) → native file dialog → picked .exe joins the catalog as a custom entry. Tray-drop and Phase-6-settings placement rejected.

### Typing Feel & Debounce
- **D-12:** **Fixed ~150ms debounce** on the file query (roadmap-mandated). Apps re-filter instantly per keystroke (unchanged 03 pipeline); file query fires once typing pauses.
- **D-13:** **Quiet fill-in** during the debounce window — apps show immediately, file rows appear when the query lands. No spinner, no indicator row.
- **D-14:** Empty query = **apps only** (03 D-01/D-02 unchanged — full alphabetical app list, first row selected). File search engages only when the query is non-empty.
- **D-15:** **Generation counter** prevents stale results (roadmap criterion 4) — results from any but the latest generation are dropped; extends the established AppCatalog worker pattern (QtConcurrent + QFutureWatcher on the UI thread).

### Indexer Trouble UX
- **D-16:** Indexer trouble detected **on-query** — lazily checked on the worker when a file query fires (ISearchCatalogManager state). Zero cost when idle; no polling on open.
- **D-17:** **3 distinct trouble states**, each with its own message: (1) indexer disabled/stopped; (2) indexer still building (first-run; conveys "files will appear soon"); (3) indexer unavailable (service gone / query failed).
- **D-18:** Trouble message = **non-selectable status row in the list area** (same space as "No results for...", Theme tokens only, visible while query non-empty and file search troubled). Apps keep working; no modal, no transient hint.

### OpenCode's Discretion
- Exact status-row copy for the three trouble states (follow the 03 empty-state copy style; RESEARCH §7 verbatim-copy discipline applies), generation-counter plumbing details, .exe filter mechanism (AQS restriction vs post-filter — planner's call per STACK API map), launch-tracking persistence format in the existing INI (`%APPDATA%\TID\wisp\wisp.ini`), file row roles in ResultsModel, Ctrl+Enter folder-open mechanism (ShellExecuteEx explorer /select, path), worker-thread handoff for file results.

### Folded Todos
None — todo list is empty (verified via `list-todos`).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase Contract & Scope
- `.planning/ROADMAP.md` — Phase 4 goal, 5 success criteria (debounce 120-150ms, generation counter, worker-thread OLE DB, fresh ISearchQueryHelper per query, degraded-state UX). Phase boundary is fixed; the user narrowed file types to .exe (see <domain>).
- `.planning/REQUIREMENTS.md` — LAUN-02 (file search via Windows Search index; Enter opens with default app), LAUN-03 (Ctrl+Enter opens containing folder).

### Prior Phase Contracts (consume, don't re-discuss)
- `.planning/phases/03-app-search-result-model-app-catalog/03-CONTEXT.md` — D-01..D-13 (empty-query = full app list first-row-selected, ranking ladder, worker catalog, dedupe, launch policy incl. D-12 snapshot freeze + D-13 instant hideNow dismissal); ResultsModel/AppEntry contracts; MatchRangesRole shape (Phase-5 highlight contract).
- `.planning/phases/02-global-hotkey-toggle/02-CONTEXT.md` — D-02.1 resident lifecycle, D-02.4 `hideNow()` instant dismissal, controller contracts.
- `.planning/phases/01-core-shell/01-UI-SPEC.md` — Approved shell design: 640×400, 44px rows, Theme.qml token singleton (no literal values ever), empty-state copy style.

### Research (locked stack & Windows APIs)
- `.planning/research/STACK.md` — File search map (HIGH confidence): `ISearchManager` → `GetCatalog(L"SystemIndex")` → `ISearchCatalogManager::GetQueryHelper()` → `ISearchQueryHelper::GenerateSQLFromUserQuery()` (AQS) + `get_ConnectionString()` → OLE DB on a worker thread; fresh ISearchQueryHelper per query; indexer-status via ISearchCatalogManager state. Deferred spike: OLE DB row consumption in C++ (ATL CDataSource/CCommand vs ADO — MS DSearch sample is C#).
- `.planning/research/ARCHITECTURE.md` — Project structure (`src/win/` Win32/COM firewall behind `src/core/`), threading/model patterns.
- `.planning/research/PITFALLS.md` — Applicable pitfalls (worker-thread COM init, model/view data-changed discipline, thread-affinity).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/core/ResultsModel.{h,cpp}` — QAbstractListModel with roles DisplayName/Subtitle/MatchRanges/Aumid; `setQuery`/`selectedIndex`/`snapshotSelected`; Q_PROPERTY query (empty-state interpolation). File entries extend this model (new source tag + roles for path/type).
- `src/core/AppEntry.h` — phase-3 entry contract: Source {Lnk, Uwp}; displayName/targetPath/arguments/aumid/iconRef. File entries need a new Source (e.g. File) or a parallel struct.
- `src/core/LaunchController.{h,cpp}` — launchSelected/launchIndex with D-12 snapshot freeze, D-13 dismissal via setDismissHandler(hideNow); WinLaunch firewall (ShellExecuteEx open/runas + UWP activation) — file launch reuses ShellExecuteEx open path; Ctrl+Enter folder-open is new.
- `src/app/main.cpp` — wiring host: catalog worker, context properties (resultsModel/launchController), visibleChanged→ensureFresh, catalog.start() once. File-query worker + indexer-status wire in here.
- `src/core/AppCatalog.cpp` — established worker pattern: QtConcurrent::run + QFutureWatcher, COM init discipline on the worker — the file-search worker mirrors this.
- `qml/MainWindow.qml` + `qml/ResultsRow.qml` — 44px Theme-token delegate, empty/no-match states, admin-refusal hint, keyboard contract via Keys.forwardTo. File rows extend ResultsRow (or a variant); "Add executable…" row + indexer status row slot in the list area.
- `wisp_core` static lib + `tests/tst_*` Qt-Test pattern — file search gets its own tst (OLE DB/COM faked or fixture-driven per 03 conventions).

### Established Patterns
- Controller-owned policy + thin QML (03): policy in C++, QML renders + forwards keys.
- Windows detail behind `src/win/` firewall with pure C++ interfaces for testability (WinHotkey/WinFullscreenGuard/WinLaunch precedent).
- Worker-thread + COM init discipline (AppCatalog: CoInitializeEx multi_threaded; watcher on UI thread).
- No registry scraping; no QProcess for launch; Theme-token-only QML (hard rules from STACK/03).

### Integration Points
- `qml/MainWindow.qml` search TextField `onTextChanged → resultsModel.setQuery` — the file-query debounce + worker hooks onto this pipeline; generation counter stamps each setQuery.
- `src/core/ResultsModel.cpp` — query dispatch point for the file pipeline + merged result assembly.
- `src/app/main.cpp` — file worker construction + indexer-status plumbing alongside the catalog.
- `src/core/LaunchController.cpp` — file/folder launch paths (Enter = open, Ctrl+Enter = Explorer select) behind the same D-12/D-13 policy.

</code_context>

<specifics>
## Specific Ideas

- User's own example: **WoW.exe on an unindexed SSD** must be reachable — the manual "Add executable…" action is the bootstrap for such apps.
- The user wants wisp to stay an **application launcher** — .exe-only file search keeps results launchable; documents/media are not the product (deferred).
- User accepted all recommended options except where noted (file types → .exe-only freeform; scope → index default + tracked/added exes freeform; folders → distinct glyph variant of the recommendation; add-exe UI → result-row action).
- PowerToys Run remains the reference feel for launcher behavior (03); Windows Search COM is the sanctioned backend (STACK — project decision, not Everything SDK).

</specifics>

<deferred>
## Deferred Ideas

- **Broader file search** — non-.exe documents/media via the same pipeline ("only .exe files for now" — user). Revisit after the launcher proves the pipeline; likely a filter + subtitle/row tweak.
- **Recency ranking + recent apps on empty query** (LAUN-07/08, v2) — the wisp launch-tracking from D-10 feeds this later.
- **App Paths / shell-history discovery** — rejected for now (registry parsing + privacy); the manual add + wisp tracking covers the use case.
- **Backdrop blur** (VISU-04, v2) — unchanged.

</deferred>

---

*Phase: 4-File Search*
*Context gathered: 2026-08-10*
