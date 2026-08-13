# Phase 7: Self-Managed File Scan - Context

**Gathered:** 2026-08-14
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 7 **drops Windows Search entirely** and replaces the Phase-4 file-search backend with wisp's own directory scanner: the user selects directories/partitions (roots) in Settings, and wisp walks them for `.exe` files (plus folder names), keeping a persistent low-footprint index. Typing fuzzy-matches the cached index on the existing debounced worker pipeline; Enter/Dedup/Launch behaviors from Phase 4 continue unchanged. The scan engine must be **"very optimized and fast with a cache and not much memory/CPU usage"** (user), with an optional rescan and an incremental re-walk that only touches directories whose `LastWriteTime` changed.

**USER SCOPE DECISIONS (2026-08-14, Q&A):** User *"doesn't want it to use Windows Search at all"* — the COM/OLE DB pipeline is removed, not just bypassed. Scanning is **`.exe`-only** files plus **folders** (folder rows from 04 D-04 survive — walking passes through directories anyway, folder names are indexed nearly free). **No default scan roots** — the index starts empty and shows a prompt until the user picks locations. A **fixed internal skip-list** of noisy/hidden/system directories applies (not user-editable). Rescan = **interval + manual**: 10-minute default (adjustable), incremental `LastWriteTime`-based re-walk, plus a "Scan now" button.

**Success criteria (user ask, adapted):** (1) file results come from wisp's own index of user-selected roots — Windows Search API is never called; (2) typing yields .exe file + folder results merged with app results (04 D-01..D-05 reused); (3) index persists across sessions — relaunch is instant with no re-walk; (4) scanning is incremental + background (low CPU/memory), never stutters typing; (5) Settings has a "Scan locations" section: root list add/remove (native folder picker), interval selector, Scan now, last-scan time + entry count; (6) same .exe in both catalog and scan results renders once — app row wins.

**Requirement:** LAUN-02, LAUN-03 (re-interpreted: "file search" now served by the local index). **Not in scope:** non-.exe file search (unchanged deferral), user-editable skip lists, partition-level wildcards, icons/monogram changes (Phase 5 glyph behavior reused), pruning of dead entries from removed/deleted exes beyond what a re-walk naturally does.

</domain>

<decisions>
## Implementation Decisions

### Backend Replacement
- **D-01:** **Windows Search is removed, not fallbacked.** `src/win/WinSearchQuery.*` (OLE DB COM, ISearchQueryHelper, AQS) is deleted; `FileSearch`'s `QueryFn`/`StatusFn` seams are re-wired to the local index instead (the 04 status-row states Disabled/Building/Unavailable are superseded by scan states — exact state set is planner's call, must never show a blank dead-end per 04 D-16..D-18 spirit).
- **D-02:** **.exe-only file entries** (04 D-09 continues) **plus folder entries** (04 D-04 continues — folders rendered with the distinct text glyph, Enter opens in Explorer). No other file types.
- **D-03:** **Dedupe across sources, app row wins:** if a scanned .exe duplicates an app-catalog entry (Start Menu lnk/UWP/tracked/added) at the same resolved path, one row renders — the catalog row (icon, display name) survives, the scanned-file row is suppressed. Catalog-vs-UWP dedupe (03) remains as-is.

### Roots & Scan Semantics
- **D-04:** **No default roots.** Fresh install / empty roots = empty file index + a friendly non-selectable status row prompting the user to open Settings and add scan locations (follows 04 D-18 status-row pattern; copy follows 03 empty-state style).
- **D-05:** Roots are **user-selected directories/drives only** (no wildcards, no file patterns). Native folder picker — `QFileDialog::getExistingDirectory` precedent exists at `main.cpp` (04 D-11 used a native file dialog for "Add executable…").
- **D-06:** **Fixed internal skip-list** (e.g. `Windows`, `ProgramData`-style system/noisy dirs, hidden & system attribute dirs skipped) during the walk. NOT user-editable in v1.
- **D-07:** **Persistent index on disk** (e.g. `%APPDATA%\TID\wisp\` — JSON or compact format, planner's call; must load in well under launch budget). In-RAM: paths + match keys only. Existing `QSettings` INI stays the small-settings store; the index is a separate file, NOT in the INI.
- **D-08:** **Incremental re-walk:** per-directory `LastWriteTime` memoization — only changed directories re-walk on interval scans. Live adds via "Scan now" and intervals run on a low-priority worker (COM-not-required for `FindFirstFile`-style walking); typing/query path never blocked (04 worker + generation-counter pattern extends).

### Schedule & UI
- **D-09:** **Interval + manual:** default **10 minutes** (adjustable in Settings; min/max planner's call), plus a **"Scan now"** button. Background behavior: first scan after roots are added and on interval; app relaunch loads the persisted index instantly (D-07) — no full re-walk at startup.
- **D-10:** **"Scan locations" section lives in the existing Settings window** (SettingsWindow.qml + C++ controller, injected-collaborator pattern per Phase 6): root list with add/remove, interval selector, "Scan now", last-scan time + entry count. No separate page/window.

### OpenCode's Discretion
Index file format + layout (JSON vs compact binary; atomic writes), exact incremental-walk data structure (mtime table), skip-list exact contents, scan state set + copy (4/7 states mapped to the 04 status-row slot), worker/pool mechanics, Settings-section layout details, dedupe key (normalized full path), how "Add executable…" (D-11 04) interacts with scanned roots (planner: keep as-is — it remains available for unscanned dirs).

### Folded Todos
None — todo list is empty (verified via `todo.match-phase 07`).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase Contract & Scope
- `.planning/ROADMAP.md` — Phase 7 entry (goal still "To be planned"; this CONTEXT defines it). Depends on Phase 6.
- `.planning/REQUIREMENTS.md` — LAUN-02 (file search; Enter opens with default app), LAUN-03 (Ctrl+Enter opens containing folder) — now interpreted against the local index.

### Prior Phase Contracts (consume, don't re-discuss)
- `.planning/phases/04-file-search/04-CONTEXT.md` — THE direct ancestor: result mix (D-01..D-05), scope (D-06..D-11), debounce/generation (D-12..D-15), status-row UX (D-16..D-18). Phase 7 keeps the pipeline, replaces the backend.
- `.planning/phases/03-app-search-result-model-app-catalog/03-CONTEXT.md` — D-01..D-13 (empty-query full app list, ranking ladder, worker catalog, dedupe, D-12 snapshot freeze, D-13 hideNow); ResultsModel/AppEntry contracts; MatchRangesRole shape.
- `.planning/phases/01-core-shell/01-UI-SPEC.md` — Approved shell: 640×400, 44px rows, Theme.qml token singleton (no literals), empty-state copy style.
- `.planning/phases/06-tray-settings-autostart-packaging/` — SettingsWindow hosting pattern (injected collaborators, Esc-hide/click-away, 120ms fade) the scan-locations section extends.

### Research
- `.planning/research/ARCHITECTURE.md` — `src/win/` firewall, `src/core/`, threading/model patterns; `src/win/WinSearchQuery.*` removal touches this layout.
- `.planning/research/PITFALLS.md` — worker-thread discipline, thread-affinity, model/view data-changed rules (scanner writes on worker → hand results to UI thread).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/core/FileSearch.{h,cpp}` — Phase-4 coordinator stays as the query pipeline: `QueryFn`/`StatusFn` seams, generation-counter debounce, worker handoff. The seams' targets change from OLE DB to the index; `Q_PROPERTY` statusText/indexerOk re-targeted to scan states.
- `src/core/ResultsModel.{h,cpp}` — roles DisplayName/Subtitle/MatchRanges/Aumid + IsFolderRole + file-entry source (04); dedupe hook (D-03) lands in result assembly here.
- `src/core/LaunchController.{h,cpp}` — launch/dismiss policy (LaunchController launchSelected, D-12/D-13, Ctrl+Enter folder-open) — unchanged by backend swap; folder-open reuses ShellExecuteEx explorer /select.
- `src/core/SettingsStore.h` + `src/ui/SettingsWindow.{h,cpp}` + `qml/SettingsWindow.qml` — Phase-6 surface; the scan-locations section (D-10) extends these. NOTE: SettingsStore is UI-thread-only QSettings INI with NO mutex — scanner state (roots/interval) may need a sibling store or a mutex before worker threads touch it (planner's call).
- `src/core/AppCatalog.cpp` — established QtConcurrent + QFutureWatcher worker pattern + Qt 6.11.1 toolchain; scanner mirrors it (no COM needed for directory walks — COM was an OLE DB requirement only).
- `src/win/WinSearchQuery.{h,cpp}` — Phase-4 COM/OLE DB pipeline: **candidate for deletion** under D-01 (check `tst_search` coverage of `classifyCatalogStatus` first — 04 tests 13/13 including it).
- `tests/tst_search` — 04 unit tests (ranker, classifyCatalogStatus); must be updated/replaced for the new backend (incremental-walk + dedupe are the new pure-logic targets).

### Established Patterns
- Controller-owned policy + thin QML; Windows detail behind `src/win/` firewall (pure C++ interfaces for testability).
- Worker-thread + watcher-on-UI-thread; generation-counter staleness (04 D-15) — extends to scanner handoffs.
- Theme-token-only QML; status rows non-selectable; copy in 03/04 empty-state style.
- No registry scraping; no QProcess for launch; native dialogs via QFileDialog precedent (`main.cpp` "Add executable…").

### Integration Points
- `src/app/main.cpp` — FileSearch seam wiring (currently → WinSearchQuery::queryFiles/checkIndexStatus; will → index query; WinSearchQuery construction removed), SettingsWindow collaborator injection, scanner lifecycle (start walk on interval / Scan now / first-root-added).
- `src/core/ResultsModel.cpp` — merged result assembly; D-03 dedupe; scanned-file roles.
- `qml/SettingsWindow.qml` — new "Scan locations" section bound to controller props (roots list, interval, scanNow, lastScan summary).
- `qml/MainWindow.qml` — status row states re-targeted (no-locations prompt D-04).

</code_context>

<specifics>
## Specific Ideas

- The user's motivating example was **an unindexed SSD with apps Windows Search misses** — Phase 4 bolted on "Add executable…" as a bootstrap; Phase 7 makes the whole backend user-directed: *"search user-selected directories/partitions for executables"* + *"a page for the users to select where to scan"*.
- "Very optimized and fast, cache, not much memory/CPU, maybe an interval" (user) — the persisted index (D-07) + incremental mtime re-walk (D-08) + low-priority worker are the direct answers; planner should keep the hot-keyboard path untouched (04 D-12..D-15).
- User accepted ALL recommended options in the 2026-08-14 Q&A (folders too / persist to disk / dedupe app-wins / section in Settings).
- wisp stays an application launcher — .exe-only index keeps every file row launchable; folders are navigational (Enter opens).

</specifics>

<deferred>
## Deferred Ideas

- **Non-.exe file search** (documents/media) — still deferred; the .exe+folder filter is unchanged.
- **User-editable skip lists / ignore patterns** — fixed internal list only in v1 (D-06).
- **Drive/partition wildcard roots** ("scan D:\") — v1 roots are explicit directories/drives only; picking a drive in the folder picker covers whole partitions.
- **Recency ranking + recent apps on empty query** (LAUN-07/08, v2) — unchanged; wisp launch-tracking from 04 D-10 still feeds this later.
- **Windows Search re-adoption as an optional backend** — knowingly rejected (D-01); revisit only if user asks.

</deferred>

---

*Phase: 7-Self-Managed File Scan*
*Context gathered: 2026-08-14*