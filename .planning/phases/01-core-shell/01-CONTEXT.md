# Phase 1: Core Shell - Context

**Gathered:** 2026-08-09
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 1 delivers the runnable Qt6/QML shell of the wisp launcher: a frameless tool window centered on the primary screen that opens with a scale+fade animation holding 60fps, closes with the reverse animation, is DPI-correct on mixed-DPI setups (Qt owns PMv2), and seeds the LGPL compliance and deploy scripts so nothing is painful to retrofit later.

**Success criteria (ROADMAP):** (1) launch → small frameless widget centered on primary screen; (2) open animation ~150–200ms holding 60fps, no stutter; (3) Escape dismisses with the reverse animation before hide; (4) crisp on 100%+150% mixed-DPI monitors, Qt owns DPI (PMv2), no manual DPI calls; (5) deploy script (`windeployqt --qmldir`) produces a standalone folder; LGPL scaffold exists (dynamic-linking lock-in, `THIRD-PARTY-NOTICES.txt` stub).

**Requirement:** VISU-01. **Not in scope:** hotkey/focus/click-away (Phase 2), search UI (Phase 3), theme polish/icons (Phase 5), tray/settings/installer (Phase 6), backdrop blur (v2, VISU-04).

</domain>

<decisions>
## Implementation Decisions

### Product Identity (renamed this session)
- **D-01:** Product name is **wisp** (was "Rofi" — user renamed everything: window title, exe, copy contract). `01-UI-SPEC.md` copy contract amended accordingly. The project/repo keep the name "Rofi-Windows".
- **D-02:** Executable: **wisp.exe**.
- **D-03:** QSettings org = **TID**, app = **wisp** → config root `%APPDATA%\TID\wisp` (or registry `HKCU\Software\TID\wisp`; exact format decided in Phase 2). Seeds the single-instance pipe name later.
- **D-04:** Version **0.1.0**, with a **VERSIONINFO resource embedded now** (FileVersion 0.1.0, ProductName "wisp", CompanyName "TID") so Task Manager/autostart entries look right from day one.

### Dev Environment & Build Workflow
- **D-05:** Toolchain verified on this machine: VS 2022 Community (C++ tools) ✓, CMake 4.3.4 on PATH ✓, Ninja ✓ (VS-bundled at `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`). **Qt on machine is 6.5.3 `msvc2019_64` + 5.15.2 — NOT the locked 6.11.1.** Phase 1 MUST include a Qt 6.11.1 install task.
- **D-06:** Install Qt 6.11.1 via **aqtinstall** (`pip install aqtinstall`; `aqt install-qt windows desktop 6.11.1 win64_msvc2022_64` into `C:\Qt`). Exact-version pinning, scriptable, no GUI. (aqt traces already present in `%APPDATA%\aqt`.)
- **D-07:** **CLI-first** build workflow: scripted `cmake --preset` + Ninja (`build.ps1`) and `windeployqt --qmldir` deploy (`deploy.ps1`). Qt Creator (already installed at `C:\Qt\Tools\QtCreator`) is optional for QML editing only — never the primary build driver.

### Lifecycle & Dismissal
- **D-08:** **Escape → close animation → hide → app quits.** No resident/hidden process in Phase 1 (no tray until Phase 6, no hotkey until Phase 2 — a hidden window would be unsummonsable). Resident lifecycle arrives with Phase 2's hotkey.
- **D-09:** **Alt+F4 / window-close requests use the same path**: close animation, then quit. One consistent exit; the window is always dismissed with animation in this phase.
- **D-10:** Open animation starts from `onVisibleChanged` (per UI-SPEC); window must be visible and animating within 100ms of the show request — animation is polish, never a gate on anything.

### Locked by Approved UI-SPEC (`01-UI-SPEC.md` — MUST honor)
- Window: `Qt.Tool | FramelessWindowHint` (never `Qt::Popup`), **640×400 fixed** (no resize), centered on primary screen `availableGeometry().center()`, `color: "transparent"` + C++ `Qt::WA_TranslucentBackground`, 12px-radius `#1E1E1E` surface, 1px `#3F3F46` border, static pre-rendered shadow (~16px, black 45%) — **no blur in the animated subtree**.
- Animation contract (VISU-01): open 150ms `OutCubic` / close 140ms `InCubic`, scale 0.96→1.0 and opacity 0→1 in `ParallelAnimation`, animating **only** `opacity` + `Scale` transform; hide happens in the close animation's `onCompleted`, never mid-animation.
- `Theme.qml` singleton is a Phase 1 deliverable carrying ALL tokens (colors, 4px spacing grid incl. 44px row + 12px radius exceptions, Segoe UI Variable→Segoe UI typography, animation durations/easings) so Phases 3/5 never introduce literal values.
- DPI: Qt PMv2 default — **never call `SetProcessDpiAwareness*` or any manual DPI API anywhere** (incl. main.cpp); all sizes logical.
- LGPL: dynamic linking only; `THIRD-PARTY-NOTICES.txt` stub seeded now; deploy script seeds the standalone-folder workflow.
- Window title / accessibility name: **"wisp"** (amended this session).
- Demo scope: app launch shows the window immediately with open animation; Escape is the only dismissal.

### OpenCode's Discretion
- Scaffolding depth: follow `research/ARCHITECTURE.md` structure (`src/app`, `src/core`, `src/win` firewall, `qml/`, `tests/`, `packaging/`) — minimal-but-forward-compatible (Phase 2+ services can slot in without rework). `src/win` stays empty-or-stub this phase (no Win32 work in Phase 1).
- CMake layout: single vs subdirectory CMakeLists, preset names, Qt module list (`Qt6::Quick`, `Qt6::Qml`, `Qt6::Gui`, `Qt6::Core` — Widgets not needed until Phase 6 tray).
- Deploy script details: exact script location/naming, whether it also stages `THIRD-PARTY-NOTICES.txt`/`vc_redist.x64.exe` now vs Phase 6.
- 60fps verification mechanism (UI-SPEC perf guard: frame-time logging in debug builds) — implementation approach free.
- Optional Qt Test smoke test for the shell.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase Contract & Scope
- `.planning/ROADMAP.md` — Phase 1 goal, 5 success criteria, VISU-01 mapping. Phase boundary is fixed.
- `.planning/REQUIREMENTS.md` — VISU-01 requirement text.
- `.planning/phases/01-core-shell/01-UI-SPEC.md` — **Approved UI design contract (geometry, animation, tokens, copy). Amended: product renamed to wisp.** Must be honored; the phase's visual deliverables are defined by it.
- `.planning/STATE.md` — Locked decisions: window flags (`Qt.Tool | FramelessWindowHint`, never Popup), Qt 6.11.1 open-source dynamic-only (never 6.8 LTS), Qt owns DPI (PMv2), deploy script + notices stub seeded in Phase 1, project structure per ARCHITECTURE.md.

### Research
- `.planning/research/SUMMARY.md` — Phase 1 implications (delivers list, perf contract: first show <100ms, 60fps animations).
- `.planning/research/ARCHITECTURE.md` — Project structure, component responsibilities, build order, Qt threading/model patterns, anti-patterns (Qt::Popup, DPI mistakes).
- `.planning/research/PITFALLS.md` — #7 (DPI/mixed monitors), #8 (LGPL compliance), #9 (windeployqt gaps) apply to this phase.
- `.planning/research/STACK.md` — Locked stack: Qt 6.11.1, MSVC 2022, CMake ≥3.25 + Ninja, windeployqt `--qmldir`, VC redist licensing.
- `.planning/research/FEATURES.md` — Product context; the scale+fade animation and dark theme are the phase-relevant differentiators.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- None — the repository contains only `.planning/` docs and `AGENTS.md`. This is a greenfield scaffold phase.

### Established Patterns
- All patterns come from research docs (see canonical refs): `Qt.Tool | FramelessWindowHint` window (Launchy-proven), QML `ParallelAnimation` on opacity+Scale only, `Theme.qml` token singleton, PMv2-only DPI.
- Git repo exists (no commits yet beyond planning docs; `git rev-parse --is-inside-work-tree` = true).

### Integration Points
- `main.cpp` + `qml/MainWindow.qml` + `qml/Theme.qml` are the new integration points; later phases add `src/core` services and `src/win` wrappers behind the controller.

</code_context>

<specifics>
## Specific Ideas

- Core value phrasing: "neat, sweet, and smooth" — the shell's animation is the product's visible identity.
- Product renamed to **wisp** during this discussion (was "Rofi"); rename applies to UI copy, exe, VERSIONINFO, window title.
- User previously used aqtinstall (traces in `%APPDATA%\aqt`) — aqtinstall is the known-comfortable install path.
- Qt Creator already installed (`C:\Qt\Tools\QtCreator`) for optional QML editing.
- Machine Qt (`6.5.3 msvc2019_64`) must NOT be used — research locked 6.11.1 (6.8 LTS = commercial-only patches; 6.5.x is past support).

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope. (Scaffolding-depth specifics left to planner discretion, recorded in OpenCode's Discretion.)

</deferred>

---
*Phase: 1-Core Shell*
*Context gathered: 2026-08-09*
