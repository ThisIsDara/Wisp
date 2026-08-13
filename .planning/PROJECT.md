# Rofi-Windows

## What This Is

A Rofi-style application launcher for Windows. A configurable global hotkey opens a small, centered widget with a search bar; typing fuzzy-matches installed apps and indexed files, Enter launches the result, and the widget dismisses on Escape, launch, or click-away. Built to feel instant and polished — "neat, sweet, and smooth."

## Core Value

The launcher must open and launch in under a second with buttery-smooth animation. Speed and feel are the product — anything that makes it feel sluggish defeats its purpose.

## Requirements

### Validated

<!-- Shipped and confirmed valuable. -->

- [x] Global hotkey (default Alt+Space) toggles the launcher; hotkey configurable by user — Validated in Phase 2 (conflict surfacing, fullscreen guard, re-register live)
- [x] Centered widget opens with scale+fade animation (~150-200ms, 60fps), reverses on dismiss — Validated in Phase 1 + 3 (deferred C++ re-center, 140-150ms durations, constant-speed key scrolling)
- [x] Fuzzy search of installed apps (Start Menu shortcuts + UWP/Store apps) — Validated in Phase 3 (vertical slice: calc→Calculator #1, launch + run-as-admin, UAC-cancel quiet)
- [x] File search via Windows Search index; results open with their default app — Validated in Phase 4 (merged .exe/folder rows, Ctrl+Enter reveal, add-executable, indexer status rows)
- [x] Enter launches selected result; Escape dismisses; click outside dismisses — Validated in Phase 3 (LAUN-05 keyboard contract, transient admin hint)

### Active

<!-- Current scope. Building toward these. -->

- [ ] Sleek dark theme with accent color, rounded corners, blur/transparency backdrop
- [ ] System tray icon with quit/settings; optional autostart with Windows
- [ ] Installer + docs for public release; works on clean Windows 10/11 installs

### Out of Scope

<!-- Explicit boundaries. Includes reasoning to prevent re-adding. -->

- Window switching (alt-tab style) — Rofi feature, but apps+files is the user's v1 scope
- Plugin system (clipboard, calculator, etc.) — defer; keep the core tight
- Cross-platform (Linux/macOS) — this is a Windows launcher; Qt6 keeps the door open but no cross-platform work in scope
- Web-runtime stack — Qt6 chosen over Electron/Tauri for native feel and binary size

## Context

- The user is a Rofi (Linux launcher) user who wants the same experience on Windows.
- Stack: **Qt6 + QML** for the UI (buttery CSS-like animations, native speed, small binary) with C++ for Windows integration.
- Windows integration is Win32/COM territory regardless of UI framework: `RegisterHotKey` for the global hotkey, Start Menu/UWP enumeration for apps, Search index queries for files.
- Public release shapes defaults: no hardcoded paths, installer, LGPL compliance for Qt (dynamic linking + license text).
- Reference points: PowerToys Run, Launchy, Albert, and Rofi itself define the interaction patterns users will expect.

## Constraints

- **Tech stack**: Qt6 + QML (UI) + C++ (Windows integration) — chosen for animation quality and native feel
- **Licensing**: Qt LGPL — must dynamically link Qt, include license notices for public release
- **Platform**: Windows 10/11 only
- **Performance**: open/close animations must hold 60fps; perceived open latency should feel instant
- **Compatibility**: must work on clean Windows installs — no hardcoded user paths, sensible defaults

## Key Decisions

<!-- Decisions that constrain future work. Add throughout project lifecycle. -->

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Qt6 + QML over Tauri/Electron/WinUI 3 | Buttery QML animations, native speed, small binary, no web runtime dependency | — Pending |
| Search apps + files via Windows Search index | User's explicit choice over folder-walking or deferring files | — Pending |
| Scale+fade open/close animation | Rofi feel; easily holds 60fps in QML | — Pending |
| Configurable hotkey, default Alt+Space | Public release requires user-configurable shortcut | — Pending |
| Tray icon + autostart | Launcher lives in background between invocations | — Pending |
| Public release (installer + docs) | Shapes defaults, licensing, and packaging from the start | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-08-10 after Phase 4 completion (file search shipped, 4/6 phases done)*
