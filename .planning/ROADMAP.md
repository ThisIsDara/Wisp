# Roadmap: Rofi-Windows

## Overview

Six phases take the project from an empty folder to a released Windows launcher. Each phase leaves a runnable, demoable app. Phase 1 builds the shell (window + animation + compliance seed); Phase 2 makes the hotkey muscle memory work (toggle, focus, dismissal, conflict surfacing, fullscreen guard); Phase 3 delivers the first vertical slice (fuzzy app search → navigate → launch); Phase 4 adds file search on the proven pipeline; Phase 5 makes it look like a product (dark theme, accent, match highlighting, icons); Phase 6 turns it into a resident citizen (tray, settings, autostart) and ships it (installer + LGPL verification on clean VMs). The two riskiest components — window focus/hotkey behavior and Windows Search COM — get dedicated phases with hard acceptance criteria. Performance contract throughout: first show < 100ms, animations 60fps, file queries never block typing.

## Phases

- [x] **Phase 1: Core Shell** - Qt6/QML scaffold, frameless centered widget, scale+fade animation, DPI correctness, LGPL + deploy script seeds
- [x] **Phase 2: Global Hotkey & Toggle** - RegisterHotKey toggle, first-show typing, dismissal (Esc/launch/click-away), conflict surfacing, fullscreen guard
- [x] **Phase 3: App Search (Result Model + App Catalog)** - Fuzzy app search vertical slice: model, .lnk + UWP catalog, ranker, keyboard nav, launch + run-as-admin
- [x] **Phase 4: File Search** - Windows Search COM pipeline: worker-thread queries, debounce + generation counter, open default app, Ctrl+Enter folder, degraded-state UX
- [x] **Phase 5: Theme & Visual Polish** - Sleek dark theme, accent-colored selection + match highlighting, async icons (completed 2026-08-10)
- [x] **Phase 6: Tray, Settings, Autostart & Packaging** - Tray + single-instance, settings window, autostart, clean-machine NSIS installer, LGPL verification (completed 2026-08-11)

## Phase Details

### Phase 1: Core Shell
**Goal**: A runnable, demoable Qt6/QML shell — the launcher widget itself: frameless tool window centered on screen, opening with a scale+fade animation holding 60fps, DPI-correct on any monitor setup, with LGPL compliance and the deploy script seeded so nothing is painful to retrofit later.
**Depends on**: Nothing (first phase)
**Requirements**: VISU-01
**Success Criteria** (what must be TRUE):
  1. User launches the app and a small frameless widget appears centered on the primary screen
  2. Opening plays a scale+fade animation (~150-200ms) that holds 60fps — smooth, no stutter
  3. Dismissing the widget (demo trigger: Escape) plays the reverse animation before the window hides
  4. UI renders crisply on mixed-DPI monitors (e.g., 100% + 150%) — no blurry text, no wrong sizing; Qt owns DPI awareness (PMv2), no manual DPI calls
  5. The deploy script (`windeployqt --qmldir`) produces a folder that runs standalone without Qt dev tools; LGPL scaffold exists (dynamic-linking lock-in, `THIRD-PARTY-NOTICES.txt` stub)
**Plans**: 01-01 ✓ (toolchain + scaffold), 01-02 ✓ (Theme singleton + shadow + animated shell), 01-03 ✓ (notices + deploy + frame probe + tests) — 3/3 done
**Status**: COMPLETE — UAT 7/7 pass (2026-08-09)
**UI hint**: yes

### Phase 2: Global Hotkey & Toggle
**Goal**: The product's muscle memory works: a global hotkey summons the launcher with keyboard focus ready, dismisses it on Escape/launch/click-away, surfaces registration conflicts instead of failing silently, and never steals focus from fullscreen games.
**Depends on**: Phase 1
**Requirements**: HOTK-01, HOTK-02, HOTK-03, HOTK-04
**Success Criteria** (what must be TRUE):
  1. Pressing Alt+Space from any application toggles the launcher — shows when hidden, hides when visible
  2. On first hotkey show, user can type immediately — zero clicks required (ordered show→raise→requestActivate, deferred off the WM_HOTKEY path)
  3. User can dismiss the launcher with Escape, by clicking outside it, and via a launch action — launch dismissal is instant, no animation wait
  4. If Alt+Space is already registered by another app, user sees a tray notification with a path to change the hotkey; the newly configured hotkey re-registers and works
  5. Pressing the hotkey while an exclusive-fullscreen game is active does not minimize the game or steal its focus (SHQueryUserNotificationState guard)
**Plans**: 02-01 ✓ (Win32 hotkey core + tst_hotkey), 02-02 ✓ (LauncherController + resident shell + tst_launcher), 02-03 ✓ (tray + capture dialog + integration + tst_tray/tst_capture)
**Status**: COMPLETE — automated validation 9/9 + smoke run (2026-08-09); manual-only UX items (real Alt+Space toggle, live conflict, fullscreen game) recorded in 02-VALIDATION.md for user sign-off
**UI hint**: no

### Phase 3: App Search (Result Model + App Catalog)
**Goal**: The first vertical slice: typing fuzzy-matches installed apps (Start Menu shortcuts + UWP/Store), results navigate and launch — including run-as-admin — backed by an in-memory catalog that builds off the hotkey path and never blocks typing.
**Depends on**: Phase 2
**Requirements**: LAUN-01, LAUN-04, LAUN-05
**Success Criteria** (what must be TRUE):
  1. User types a query and installed apps fuzzy-match — case-insensitive subsequence matching with word-boundary/camelCase bonuses; Start Menu .lnk and UWP/Store apps both appear
  2. Golden-list check: Calculator, Terminal, and Notepad are findable; no framework/junk entries (AppListEntry=none filtered); no duplicates between .lnk and UWP sources
  3. User navigates results with ↑/↓, PageUp/PageDown, Home/End and launches with Enter; mouse click also launches; selection freezes on Enter (no launch of a shifted item)
  4. Ctrl+Shift+Enter launches the selected app as administrator; UWP/Store apps gracefully refuse
  5. Typing stays instant: results appear without perceptible delay per keystroke (in-memory tier-1, <5ms), and first hotkey show stays under ~100ms — catalog builds asynchronously, never in the hotkey path
  *(Design note: the fuzzy ranker must return match positions (ranges) from day one — LAUN-06 highlighting in Phase 5 consumes them.)*
**Plans**: 03-01 ✓ (AppEntry contract + FuzzyMatcher + ResultsModel + tst_matcher/tst_model), 03-02 ✓ (.lnk + UWP enumerators + tst_enum), 03-03 ✓ (AppCatalog worker/dedupe + tst_catalog), 03-04 ✓ (WinLaunch + LaunchController + tst_launch), 03-05 ✓ (main.cpp wiring + QML search/list + human-verify checkpoint)
**Status**: 5/5 plans COMPLETE (ctest 10/10, human-verified: calc→Calculator #1, UAC-cancel quiet, UWP admin-refusal hint, centered opens, constant-speed key scrolling; defects resolved: top-left placement, Esc/click-away after first close, query persistence, hover vs key auto-repeat) | 2026-08-10
**UI hint**: yes

### Phase 4: File Search
**Goal**: Files join the search: Windows Search index queries run on worker threads with debounce and stale-result dropping, files open with their default app, containing folder opens with Ctrl+Enter, and indexer problems are explained rather than silently empty.
**Depends on**: Phase 3
**Requirements**: LAUN-02, LAUN-03
**Success Criteria** (what must be TRUE):
  1. User types a filename and matching file results from the Windows Search index appear, merged with app results
  2. Enter on a file result opens it with its default app
  3. Ctrl+Enter on a file result opens the containing folder in Explorer with the file selected
  4. Typing never freezes or stutters while file queries run; stale results from earlier keystrokes never appear (debounce ~120-150ms + generation counter + worker-thread OLE DB, fresh ISearchQueryHelper per query)
  5. When the indexer is still building or disabled, user sees a distinct, friendly message explaining the state — never a blank dead-end
**Plans**: 5 plans
- [x] 04-01-PLAN.md — Windows Search query firewall: raw OLE DB COM pipeline + tst_search (status mapping, .exe/folder filter, WHERE restriction)
- [x] 04-02-PLAN.md — FileSearch coordinator: 150ms debounce, generation counter, worker dispatch, tracked-source merge, locked status copy + tst_filesearch
- [x] 04-03-PLAN.md — Launch side: LaunchHistory (INI tracking + manual add) + WinLaunch::revealInExplorer + controller D-05/reveal policy + tst_history/tst_launch
- [x] 04-04-PLAN.md — ResultsModel merge: one interleaved score-desc list, 5-file cap, path-only base tier, IsFolderRole, stale-generation guard + tst_model
- [x] 04-05-PLAN.md — Wiring + QML: main.cpp seams, status row (D-18), Ctrl+Enter reveal, folder glyph, "Add executable…" row + human checkpoint (approved 8/8 + selection-indicator note)
**Status**: 5/5 plans COMPLETE (ctest 13/13, human-verified 8/8 + selection-bar note; code review 5 warnings fixed: WR-01..WR-05) | 2026-08-10
**UI hint**: no

### Phase 5: Theme & Visual Polish
**Goal**: The launcher looks like a product: sleek dark theme, accent-colored selection and match highlighting, real icons loaded asynchronously — the polish that makes the "instant and smooth" feel visible. (Backdrop blur is v2 — VISU-04.)
**Depends on**: Phase 4
**Requirements**: VISU-02, LAUN-06
**Success Criteria** (what must be TRUE):
  1. The launcher UI renders in a sleek dark theme — dark surfaces, readable contrast, rounded corners, no light-mode leftovers
  2. Matched characters in results are highlighted in the accent color (using ranker match positions from Phase 3)
  3. The selected result is visually distinct, rendered with the accent color
  4. App and file results show correct icons, loaded asynchronously with no UI freeze; UWP icons resolve properly (indirect strings + scale variants) and cache without unbounded growth
  *(Design note: the accent value lives in the settings store; Phase 6 adds the user-facing picker that changes it live.)*
**Plans**: TBD
**UI hint**: yes

### Phase 05.1: Catalog curation - show real apps and games only (INSERTED)

**Goal:** The default catalog shows only real apps and games the user actually uses — a curated allowlist of ~280 well-known apps/games (user-directed at checkpoint: blocklist → allowlist) defaults everything else hidden, a persisted per-row Hide/Unhide prunes whatever the rules miss, hidden entries stay recoverable via a "Show hidden (N)" mode + right-click menu, and the existing "Add executable…" path remains the allowlist escape hatch. Curation is a pure marking step in the catalog pipeline (post-dedupe, pre-sort), never a security boundary. User overrides always beat rules.
**Requirements**: CUR-01, CUR-02, CUR-03, CUR-04
**Depends on:** Phase 5
**Plans:** 4 plans

Plans:
- [x] 05.1-01-PLAN.md — Curation core: default rules engine (CurationRules) + persisted hide/show store (CurationStore) + tst_curation
- [x] 05.1-02-PLAN.md — AppCatalog curation seam: CurationData/CurationSource injection + markCurated (post-dedupe, pre-sort) + tst_catalog
- [x] 05.1-03-PLAN.md — ResultsModel hide/unhide/show-hidden surface (IsHiddenRole, HideStore seam, query-preserving live removal) + tst_model
- [x] 05.1-04-PLAN.md — Wiring + UI: main.cpp seams, Ctrl+H, "Show hidden (N)" footer, dimmed rows + human acceptance checkpoint

### Phase 6: Tray, Settings, Autostart & Packaging
**Goal**: The app becomes a resident citizen: tray icon with Open/Settings/Quit and single-instance, settings window (hotkey capture, accent color picker, autostart toggle), start-with-Windows, and a clean-machine installer with LGPL compliance verified — the release gate.
**Depends on**: Phase 5
**Requirements**: SYS-01, SYS-02, SYS-03, SYS-04, VISU-03
**Success Criteria** (what must be TRUE):
  1. Tray icon is present with Open/Settings/Quit; launching a second instance surfaces the existing instance instead of duplicating
  2. User can toggle "start with Windows" in settings; after sign-out/sign-in the launcher starts automatically with tray present (quoted HKCU Run key, `--autostart` arg)
  3. Settings window: user can capture a new hotkey (re-registers and works immediately), pick an accent color (applies live to selection and match highlighting), and toggle autostart
  4. Installer works on clean Win10 22H2 and Win11 24H2 VMs (no dev tools, no Qt): install → launch → hotkey → launch an app → search a file; hotkey-conflict notification re-verified with another launcher owning Alt+Space
  5. LGPL compliance verified: `THIRD-PARTY-NOTICES.txt` ships in the installer, Qt is dynamically linked (relink test passes), source offer documented
**Plans**: 5 plans

Plans:
- [x] 06-01-PLAN.md — Single-instance guard + autostart Run-key store (WinSingleInstance, AutostartManager, unit tests)
- [x] 06-02-PLAN.md — Settings surface QML: Theme tokens, SettingsWindow, custom ColorDialog, literal tokenization
- [x] 06-03-PLAN.md — Tray Settings action + SettingsWindow C++ controller with dismissal semantics + capture handoff
- [x] 06-04-PLAN.md — main.cpp wiring: single-instance boot guard, --autostart quiet boot, summon + accent-to-tray binding
- [x] 06-05-PLAN.md — NSIS per-user installer + LGPL compliance evidence + clean-VM validation runbook (human checkpoint)
**UI hint**: yes

## Coverage

| Requirement | Phase |
|-------------|-------|
| VISU-01 | 1 |
| HOTK-01, HOTK-02, HOTK-03, HOTK-04 | 2 |
| LAUN-01, LAUN-04, LAUN-05 | 3 |
| LAUN-02, LAUN-03 | 4 |
| VISU-02, LAUN-06 | 5 |
| CUR-01, CUR-02, CUR-03, CUR-04 | 05.1 |
| SYS-01, SYS-02, SYS-03, SYS-04, VISU-03 | 6 |

**Mapped: 21/21** — no orphans, no duplicates. v2 requirements (LAUN-07/08/09, VISU-04) deliberately excluded.

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5 → 6

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Core Shell | 3/3 | COMPLETE (UAT 7/7) | 2026-08-09 |
| 2. Global Hotkey & Toggle | 3/3 | COMPLETE (auto 9/9 + smoke; manual UX sign-off pending) | 2026-08-09 |
| 3. App Search (Result Model + App Catalog) | 5/5 | COMPLETE (vertical slice user-approved) | 2026-08-10 |
| 4. File Search | 5/5 | COMPLETE (human-verified 8/8 + note, ctest 13/13) | 2026-08-10 |
| 5. Theme & Visual Polish | 5/5 | Complete   | 2026-08-10 |
| 05.1. Catalog Curation | 4/4 | COMPLETE (user-approved, code review + verification passed, ctest 17/17) | 2026-08-11 |
| 6. Tray, Settings, Autostart & Packaging | 5/5 | Complete   | 2026-08-11 |
