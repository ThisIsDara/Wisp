# Phase 3: App Search (Result Model + App Catalog) - Context

**Gathered:** 2026-08-09
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 3 delivers the first vertical slice: typing fuzzy-matches installed apps (Start Menu .lnk + UWP/Store packages), results navigate with the keyboard and launch — including run-as-administrator — backed by an in-memory catalog that builds asynchronously, never in the hotkey path, and never blocks typing.

**Success criteria (ROADMAP):** (1) case-insensitive fuzzy subsequence matching with word-boundary/camelCase bonuses; (2) golden list: Calculator, Terminal, Notepad findable; no junk entries (AppListEntry=none filtered); no duplicates between .lnk and UWP sources; (3) ↑/↓, PageUp/PageDown, Home/End nav + Enter launch; mouse click also launches; selection freezes on Enter (no launch of a shifted item); (4) Ctrl+Shift+Enter runs the selected app as administrator; UWP/Store apps gracefully refuse; (5) typing stays instant: results <5ms per keystroke; first hotkey show stays under ~100ms — catalog builds asynchronously, never in the hotkey path.

**Requirement:** LAUN-01, LAUN-04, LAUN-05. **Not in scope:** file search (Phase 4), icons + match-highlight visuals (Phase 5 — but the ranker MUST emit match ranges now), recency/recent-apps (v2 LAUN-07/08), settings window (Phase 6), backdrop blur (v2).

</domain>

<decisions>
## Implementation Decisions

### Empty-Query Behavior
- **D-01:** Empty query shows the **full installed-app list**, alphabetically sorted (case-insensitive), computed by the same catalog — requires the catalog to be built. No cap: every filtered app appears (virtual scrolling handles overflow; the 640×400 shell shows ~8 of 44px rows).
- **D-02:** The **first row is selected by default** on show (Rofi/Spotlight feel — Enter launches the top app immediately). Mouse hover also selects (LAUN-05 click-to-launch).
- **D-03:** Column order for the empty list is plain alphabetical: exact duplicate display names broken by source rule (see D-08).

### Ranking Feel (the fuzzy matcher)
- **D-04:** Priority ladder: **exact match > name prefix > word-boundary start > any subsequence**, case-insensitive, with camelCase bonuses. Golden-list bar: `cal`→Calculator, `term`→Terminal, `note`→Notepad.
- **D-05:** **Alphabetical tie-break** (stable, predictable) — no catalog-order or image-name bonuses. (LAUN-07 recency is v2 and will layer on top.)
- **D-06:** **No score cutoff** — every subsequence match scores; single-character queries like `q` return everything containing a q, weakest last. Applied per-keystroke; in-memory filter on the UI thread must stay <5ms (STACK: in-memory tier-1).
- **D-07:** The matcher is a **pure, in-house C++ function** (~150 lines, no dependencies per STACK) that returns **score + match ranges (start/length per matched character)** — ranges are the LAUN-06 highlight data Phase 5 consumes; never re-derivable later, must exist from day one. Unit-tested standalone against the golden list.

### Catalog Refresh
- **D-08:** Catalog builds **on a worker thread at startup** (async behind the resident shell); each hotkey show checks the build age and **rebuilds only if older than ~10 minutes**. Silent swap while open (old catalog remains usable during rebuild, no UI indicator).
- **D-09:** In-memory only, no SQLite/DB (STACK: sub-100ms enumeration needs no persistence; revisit only if measurements demand).
- **D-10 (dedupe, OpenCode discretion):** On case-insensitive display-name collision between a .lnk entry and a UWP entry, **prefer the classic .lnk entry and suppress the UWP duplicate** (single result per app — ROADMAP criterion 2). UWP-only apps naturally remain. Exact full-name equality only — no fuzzy dedupe.

### Launch & Admin (LAUN-04)
- **D-11:** Classic apps launch via `ShellExecuteEx` (resolved .lnk target path). **Ctrl+Shift+Enter = `runas` verb** + `SEE_MASK_NOCLOSEPROCESS`; user-cancelled UAC is not error-spammed. **UWP/Store apps refuse admin gracefully** — a transient status hint in the shell UI ("Only desktop apps can run as administrator"), never a modal.
- **D-12:** Selection **freezes on Enter**: launch targets are snapshotted at keypress, so a result shift mid-launch can't launch the wrong app (ROADMAP criterion 3).
- **D-13:** Launch dismissal uses the existing `hideNow()` instant path (Phase-2 D-02.4, proven by tst_launcher) — no animation wait.

### OpenCode's Discretion
- Enumeration/COM structure inside `src/win/` (scanner classes behind the `wisp_core` lib pattern), exact class/method layout, worker-thread handoff mechanism (signal/slot vs pointer swap), navigation key-remapping tables, empty-state copy — planner's call within architectural conventions (ARCHITECTURE.md, STACK.md).
- Result row visual: **no icon pipeline this phase** (icons = Phase 5) — monogram/letter or generic glyph placeholder acceptable in the row until then.

### Folded Todos
None — todo list is empty (verified via `list-todos`).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase Contract & Scope
- `.planning/ROADMAP.md` — Phase 3 goal, 5 success criteria (incl. the golden list + match-ranges design note). Phase boundary is fixed.
- `.planning/REQUIREMENTS.md` — LAUN-01/04/05 requirement text; LAUN-06 (highlighting) explicitly deferred to Phase 5 with data contract here.
- `.planning/STATE.md` — Locked decisions (Qt 6.11.1, dynamic-only LGPL, PMv2, settings INI at `%APPDATA%\TID\wisp\wisp.ini`, focus sequence, controller-owned visibility).

### Prior Phase Contracts (consume, don't re-discuss)
- `.planning/phases/02-global-hotkey-toggle/02-CONTEXT.md` — D-02.1 resident lifecycle, D-02.4 `hideNow()` instant dismissal, controller contracts consumed by launch actions. 02-RESEARCH.md §6 interface contracts (parallel: worker-thread patterns).
- `.planning/phases/01-core-shell/01-UI-SPEC.md` — Approved shell design: 640×400 fixed, 44px row height, 4px spacing grid, 12px radius, Theme.qml token singleton (Phase 3/5 must never introduce literal values). Amended: product renamed to wisp.

### Research (locked stack & Windows APIs)
- `.planning/research/STACK.md` — App enumeration map (HIGH confidence): `SHGetKnownFolderPath(FOLDERID_Programs/CommonPrograms)` → recurse `*.lnk` → `IShellLinkW`+`IPersistFile` parse; UWP via `PackageManager.FindPackagesForUser(L"")` → `AppListEntry.DisplayInfo` (filter AppListEntry=none / ActiveLocation=none), launch via `IApplicationActivationManager::ActivateApplication(aumid = PackageFamilyName + "!" + appId)`; elevation via `ShellExecuteEx` `runas` (QProcess CANNOT elevate — STACK "What NOT to Use"); fuzzy matcher written in-house (exact→prefix→boundary scoring, ~150 lines); icons `IShellItemImageFactory` = Phase 5, NOT this phase.
- `.planning/research/ARCHITECTURE.md` — Project structure (`src/win/` as Win32/COM firewall behind `src/core/`), Qt threading/model patterns, build order.
- `.planning/research/PITFALLS.md` — Applicable pitfalls (worker-thread COM initialization, model/view data-changed discipline).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `qml/Theme.qml` — token singleton: colors, 44px row + 12px radius exceptions, 4px spacing grid, typography — the results list styles itself from these; no literals.
- `src/core/LauncherController.{h,cpp}` — owns ALL visibility policy; exposes `toggle()`, `showUserRequested()` (fullscreen-guard bypass), `hideNow()` instant-dismiss — launch actions call `hideNow()`, fullscreen-guard interplay is already handled.
- `src/app/main.cpp` — QApplication host + full hotkey/tray wiring; Phase 3 appends the catalog builder + model behind the same controller.
- `wisp_core` static lib + `tests/tst_*` Qt-Test pattern (5 green: shell, hotkey, launcher, capture, tray) — catalog/matcher/model get their own tst with the same wiring (`BUILD_TESTING` block in a top-level CMakeLists).
- `build.ps1` dev loop + `ctest --test-dir build/dev` (Qt bin on PATH) — established verification workflow.

### Established Patterns
- Controller-owned policy + thin QML: QML never holds logic, C++ never touches UI directly (invokeMethod/dismiss contract from 02).
- Windows detail behind `src/win/` firewall with pure C++ interfaces for testability (WinHotkey/WinFullscreenGuard precedent).
- In-memory catalog decisions need no new deps — QtConcurrent/QThread worker is the established threading tool (ARCHITECTURE.md).

### Integration Points
- `qml/MainWindow.qml` — search TextField already exists (focus contract from Phase 2: forceActiveFocus on active, Escape hides); the results ListView slots in below it inside the 640×400 shell.
- `src/app/main.cpp` — catalog worker starts alongside HotkeyManager/tray; query events flow TextField → controller → model.
- `src/core/` — new: FuzzyMatcher (pure), AppCatalog (worker-built, age check on show), ResultsModel (QAbstractListModel — LAUN-05 nav consumed via selection APIs).

</code_context>

<specifics>
## Specific Ideas

- Golden list as acceptance bar: `cal`→Calculator, `term`→Terminal, `note`→Notepad must land in top spots — carry into the plan's unit tests.
- User accepted all recommended options without freeform additions; discussion stayed fast and decisive.
- PowerToys Run declared as the reference feel for the full-list-on-empty-query behavior (its Program plugin strategy already anchors the enumeration approach in STACK.md).

</specifics>

<deferred>
## Deferred Ideas

None new — discussion stayed within phase scope. Existing v2 exclusions reaffirmed during discussion: recency ranking + recent-apps-on-empty (LAUN-07/08) deliberately NOT this phase (empty query = plain alphabetical list), icons + highlight visuals = Phase 5, file search = Phase 4.

</deferred>

---

*Phase: 3-App Search (Result Model + App Catalog)*
*Context gathered: 2026-08-09*