---
phase: 02-global-hotkey-toggle
plan: 01
subsystem: win32-hotkey, fullscreen-guard, settings
tags: [qt6, qml, win32, registerhotkey, quns, qsettings, cmake]

requires:
  - phase: 01-core-shell
    provides: Buildable wisp.exe + wisp_core-suitable CMake layout
provides:
  - WinHotkey: RegisterHotKey(NULL) + windows_dispatcher_MSG native event filter (id-filtered hotkeyTriggered signal, unregisterAll, errorString 1409 → "already in use by another application")
  - WinFullscreenGuard: QUNS-based fullscreen detection (SHQueryUserNotificationState, 2/3/4 → FullscreenActive, 5/6/7 → AcceptsNotifications, else Other)
  - HotkeyManager: INI QSettings-backed lifecycle (default Alt+Space, F12/modifier-only/unmapped rejection, register-new-revert-old swap, UnregisterOnQuit teardown)
  - wisp_core static lib shared by app + tests; tst_hotkey suite (6 tests) green
affects: [02-global-hotkey-toggle (plans 02/03), 06]

tech-stack:
  added: [wisp_core static library target]
  patterns: [Win32 wrapped behind Qt-friendly classes in src/win/, QObject+mixin base for native event filter]

key-files:
  created: [src/win/WinFullscreenGuard.h, src/win/WinFullscreenGuard.cpp, src/core/HotkeyManager.h, src/core/HotkeyManager.cpp, tests/tst_hotkey.cpp]
  modified: [src/win/WinHotkey.h, CMakeLists.txt]

key-decisions:
  - "WinHotkey inherits QObject + QAbstractNativeEventFilter (multiple inheritance) — Q_OBJECT requires QObject as a base; QAbstractNativeEventFilter alone is not a QObject (compile fix 1e034c1)"
  - "wisp_core STATIC lib (src/win/* + src/core/*) linked by wisp, tst_hotkey — app/test sharing per plan option A"
  - "HotkeyManager registers on start(); setHotkey() only while live (register-new-revert-old); boot flow in tests mirrors real app flow"
  - "Dev-session-safe test combos only (Ctrl+Alt+F9/F8) — never touch user's real Alt+Space; 1409 conflict test uses the SAME combo within one process"

patterns-established:
  - "Tests must run ctest with Qt bin on PATH (build.ps1 scopes PATH to its own cmd session): $env:Path = 'C:/Qt/6.11.1/msvc2022_64/bin;...' first"
  - "QtTest stdout is swallowed when launched via bare pwsh redirection on this box — diagnose failures with Start-Process -RedirectStandardOutput"
  - "HotkeyManager contract: start() twice or setHotkey-before-start() legitimately hits 1409 self-conflict — tests must never do that"

requirements-completed: [HOTK-01, HOTK-02, HOTK-04]

# Metrics
duration: 2h15m (incl. two compile/test fix iterations)
completed: 2026-08-09
---

# Phase 02 Plan 01: Win32 Hotkey Core Summary

Global hotkey foundation committed and tested: WinHotkey (RegisterHotKey + windows_dispatcher_MSG filter), WinFullscreenGuard (QUNS state), HotkeyManager (INI-persisted lifecycle), all linked through a new wisp_core static library with a 6-test Qt Test suite — 2/2 ctest targets green (tst_shell + tst_hotkey).

## Performance

- **Duration:** ~2h15m (includes an executor-subagent abort/resume and two fix iterations)
- **Started:** 2026-08-09 (execution breakouts, resumed after executor failure)
- **Completed:** 2026-08-09
- **Tasks:** 3 (WinHotkey / FullscreenGuard+HotkeyManager / tests+CMake)
- **Files modified:** 8 (per plan)

## Task Status

| # | Task | Commits | Status |
|---|------|---------|--------|
| 1 | WinHotkey: RegisterHotKey + native filter | `f438ffd`, `1e034c1` (compile fix) | Done |
| 2 | WinFullscreenGuard + HotkeyManager | `f9a27a0` | Done |
| 3 | tst_hotkey + wisp_core CMake wiring | `25ab8de`, `d25ebd2` (test fixes) | Done |

## Verification

- `cmake --preset dev && cmake --build --preset dev`: clean build, wisp.exe + tst_hotkey.exe produced
- `ctest --test-dir build/dev`: **2/2 passing** (tst_shell 0.24s, tst_hotkey 0.13s)
- tst_hotkey covers: registration round-trip (synthesized WM_HOTKEY wParam id filtering), 1409 conflict + errorString wording, QUNS mapping 1–999, persistence read-back without forbidden re-start, F12/modifier-only/empty rejection (memory + disk untouched), hotkey swap freeing the old combo (HOTK-01)

## Issues Encountered

1. **Q_OBJECT on non-QObject base** — WinHotkey declared `Q_OBJECT` while inheriting only `QAbstractNativeEventFilter`; moc-generated code could not compile (staticMetaObject, connectImpl). Fixed with multiple inheritance `QObject, QAbstractNativeEventFilter`. No behavioral change.
2. **ctest DLL-not-found (0xc0000135)** — Qt bin dir is scoped to build.ps1's cmd session; running ctest from the shell needs the PATH prepended.
3. **Two test bugs surfaced by first run** (product code correct):
   - `hotkeyManagerRejectsInvalid` read back a key that was never persisted (rejections before any valid setHotkey legitimately leave settings empty) — test now persists a valid combo first and asserts it stays untouched.
   - `hotkeyManagerReRegister` called setHotkey() before start(), re-registering the same combo twice → self-conflict 1409; and its "old combo free" probe registered the NEW combo. Test now follows the real boot flow (start → setHotkey while live) and probes the genuinely old combo.
4. **QtTest stdout swallowed** under bare pwsh pipes (exit 2, zero output) — Start-Process with file redirection revealed full output.

## Notes for Downstream Plans

- 02-02 consumes: `HotkeyManager::hotkeyPressed` / `registrationFailed` / `setHotkey`, `WinFullscreenGuard::State` (defer contract: FullscreenActive or Other → defer, AcceptsNotifications → show).
- 02-03 consumes: `WinHotkey` + `HotkeyManager` wiring order (tray.show() → connect registrationFailed → hotkeys.start()), theme-driven capture dialog via setHotkey.
- Hotkey id 1 is reserved by this plan (`kHotkeyId`); 02-03 must keep ids in 0x0000–0xBFFF app range.