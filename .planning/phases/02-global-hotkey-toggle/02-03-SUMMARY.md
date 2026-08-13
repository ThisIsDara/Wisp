# Phase 2, Wave 3 Summary — QApplication host, tray menu, hotkey capture, full wiring

> Status: complete. ctest 5/5 green; app runs resident (smoke-verified); manual-only UX items deferred.

## Commits

| Commit | Scope |
|---|---|
| `05ac0dc` | QApplication host (`src/app/main.cpp` QGuiApplication→QApplication) + `TrayIcon` (Open wisp / Change hotkey… / Quit menu, conflict balloon path, tray-less exit fallback) |
| `4ae69b0` | `HotkeyCaptureDialog` — themed capture QML + C++ validation host (F12/modifier-only rejected, `dialogHost` instance injection) |
| `55861d6` | Task 3 full wiring: hotkey→toggle, activation grace, tray menu signals, capture→setHotkey; `tst_capture` + `tst_tray`; plus the wave-2 `MainWindow.qml` resident-lifecycle changes that had been sitting uncommitted |

## What was built

- **`src/app/main.cpp` (rewritten)** — end-to-end phase-2 wiring:
  1. `HotkeyManager hotkeys;` → `LauncherController controller;` → `TrayIcon tray;` →
  2. `connect(hotkeyPressed → controller.toggle)`
  3. `connect(window.activeChanged → controller.onWindowActiveChanged(window.isActive()))` (adapter lambda — `activeChanged()` is parameterless)
  4. tray menu: openWisp→`showUserRequested`; changeHotkeyRequested→`capture.open(hotkeys.hotkey().toString())`; quitRequested→`app.quit`
  5. `capture.accepted → hotkeys.setHotkey`
  6. `tray.show()` → `connect(registrationFailed → notifyHotkeyConflict)` → `hotkeys.start()` (HOTK-02 order invariant)
  7. Tray-less fallback (tests/bootstrap): toggle + grace + `closing→quit` still work.
- **`src/tray/TrayIcon.{h,cpp}`** — menu ownership fixed during T3: `QObject::setParent(this)` **asserts** on widgets parented to a non-widget (`!d->isWidget`, qobject.cpp:2265) — replaced by explicit `~TrayIcon()` deleting `m_menu` + `menu()` accessor; complete includes (QMenu/QSystemTrayIcon) in the header for QPointer members.
- **`src/ui/HotkeyCaptureDialog.{h,cpp}`** + **`qml/HotkeyCaptureDialog.qml`** (Task 2) — capture flow with validation matrix; moved into `wisp_core` in T3 so `tst_capture` links (wisp exe no longer compiles it directly).
- **`tests/tst_capture.cpp`** — validation matrix (F12 anywhere; modifier-only; empty; valid combos).
- **`tests/tst_tray.cpp`** — menu structure `[Open][Change…][sep][Quit]` + three signal spies + conflict `showMessage` smoke.
- **`qml/MainWindow.qml`** — resident lifecycle (visible:false, closing-flag reset, `hideNow`, forceActiveFocus on active, no quit on close) — committed here, folding in wave-2 leftover.

## Deviations

- **Two `hotkeys.start()` calls** in main.cpp (tray branch line 85 / tray-less branch line 101). Deviation from "one start" in plan text; correct design — the tray-less fallback must still register. Plan's wiring-order grep uses `Select-Object -First 1` which matches a **comment** ("constructed BEFORE hotkeys.start()", line 49) — the checker must use the last match. Logged in 02-VALIDATION.md Execution Log.
- Test refactor: `tst_tray` menu assertion indexes separator at 2 (menu order `[Open][Change…][sep][Quit]`), not at 1-based-append position.
- Qt 6.11 qmlcachegen rejects inline `component` — concrete buttons in capture QML (qmllint-verified).

## Verification

- Build: clean via `.\build.ps1 -Config dev`.
- `ctest --test-dir build/dev --output-on-failure`: **5/5 pass** (tst_shell, tst_hotkey, tst_launcher, tst_capture, tst_tray).
- Invariant checks: tray-branch order `tray.show() < connect(registrationFailed) < hotkeys.start()` verified.
- Smoke: `wisp.exe` resident 4s+, no crash, force-killed.

## Manual-only leftovers (deferred to user validation)

Real Alt+Space toggle from a foreground app; real global conflict (another app owns combo) + balloon click path; exclusive-fullscreen deferral in a real game. All in 02-VALIDATION.md Manual-Only table.