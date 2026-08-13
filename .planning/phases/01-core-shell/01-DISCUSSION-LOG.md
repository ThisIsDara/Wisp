# Phase 1: Core Shell - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-09
**Phase:** 01-core-shell
**Areas discussed:** Dev environment & build setup, App identity, Post-Escape lifecycle

---

## Dev environment & build setup

| Option | Description | Selected |
|--------|-------------|----------|
| Already installed | Qt 6.11.1 with MSVC 2022 kit ready; scaffold directly | |
| Nothing installed yet | Phase 1 includes environment setup | |
| Partial / wrong toolchain | Older Qt or MinGW-only present; handle upgrade | ✓ (user believed installed; verified partial) |

**User's choice:** "I think they are already installed, double check if needed"
**Notes:** Verified on machine: VS 2022 Community ✓, CMake 4.3.4 ✓, Ninja (VS-bundled) ✓, but Qt is 6.5.3 `msvc2019_64` + 5.15.2 — not the locked 6.11.1. Phase 1 must install Qt 6.11.1.

| Option | Description | Selected |
|--------|-------------|----------|
| aqtinstall (Recommended) | pip aqtinstall → exact-version pinning, scriptable, C:\Qt | ✓ |
| Official online installer | MaintenanceTool GUI, bundles CMake/Ninja | |
| Decide during planning | Put off the choice | |

**User's choice:** aqtinstall
**Notes:** aqt traces already present in %APPDATA%\aqt.

| Option | Description | Selected |
|--------|-------------|----------|
| CLI-first scripts (Recommended) | build.ps1 (cmake --preset + ninja), deploy.ps1 (windeployqt --qmldir) | ✓ |
| Qt Creator primary | Kit-driven build/run/debug | |
| VS Code primary | CMake Tools extension | |

**User's choice:** CLI-first scripts
**Notes:** Qt Creator (installed) optional for QML editing only.

---

## App identity

| Option | Description | Selected |
|--------|-------------|----------|
| rofi.exe (Recommended) | Short, Unix-y, matches Rofi muscle memory | |
| RofiWindows.exe | Matches repo name, clearer in Task Manager | |
| Other single word | e.g. RofiLauncher.exe | ✓ (freeform: "single word, wisp") |

**User's choice:** wisp.exe (freeform: "single word, wisp")
**Notes:** User renamed the product to **wisp** everywhere. Confirmed via follow-up: "yes rename everything" — window title, exe, copy contract in UI-SPEC (amended). Project/repo keep the name "Rofi-Windows".

| Option | Description | Selected |
|--------|-------------|----------|
| org=wisp, app=wisp (Recommended) | Config in %APPDATA%\wisp\wisp | |
| org=author, app=wisp | Real name/handle as org | ✓ |

**User's choice:** org=author, app=wisp → org value supplied freeform: **TID**
**Notes:** Config root `%APPDATA%\TID\wisp`; seeds single-instance pipe name later.

| Option | Description | Selected |
|--------|-------------|----------|
| 0.1.0 + VERSIONINFO now (Recommended) | VERSIONINFO embedded (ProductName wisp, CompanyName TID) | ✓ |
| CMake version only | Defer VERSIONINFO to packaging | |
| 1.0.0 from the start | Ship-shaped from day one | |

**User's choice:** 0.1.0 + VERSIONINFO now

---

## Post-Escape lifecycle

| Option | Description | Selected |
|--------|-------------|----------|
| Quit after close (Recommended) | Escape → close animation → hide → app quits; no zombie process | ✓ |
| Stay resident hidden | Anticipates Phase 2 hotkey; unsummonsable without it | |
| Hybrid / something else | Custom behavior | |

**User's choice:** Quit after close
**Notes:** Resident lifecycle arrives with Phase 2's hotkey.

| Option | Description | Selected |
|--------|-------------|----------|
| Same animation, then quit (Recommended) | Alt+F4/WM_CLOSE plays close animation then quits | ✓ |
| Immediate quit on Alt+F4 | Only Escape is animated | |
| Don't care / you decide | Minimal path | |

**User's choice:** Same animation, then quit

---

## OpenCode's Discretion

- Scaffolding depth (ARCHITECTURE.md structure, minimal-but-forward-compatible; `src/win` stub or empty this phase)
- CMake layout / presets / Qt module list
- Deploy script exact shape (script location, staging of notices / vc_redist now vs Phase 6)
- 60fps verification mechanism (frame-time logging approach)
- Optional Qt Test smoke test

## Deferred Ideas

None — discussion stayed within phase scope.
