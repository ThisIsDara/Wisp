---
phase: 02
slug: global-hotkey-toggle
status: discussed
updated: 2026-08-09
---

# Phase 2 Context — Global Hotkey & Toggle

> Source of truth for what Phase 2 must deliver. Locked decisions D-02.1..D-02.6 (from 02-RESEARCH.md) are NON-NEGOTIABLE — plans implement them exactly.

## Phase Goal (ROADMAP)

The product's muscle memory works: a global hotkey summons the launcher with keyboard focus ready, dismisses it on Escape/launch/click-away, surfaces registration conflicts instead of failing silently, and never steals focus from fullscreen games.

## Requirements (from REQUIREMENTS.md)

| ID | Requirement |
|----|-------------|
| HOTK-01 | Global hotkey toggles launcher; default Alt+Space; user-configurable |
| HOTK-02 | Hotkey registration conflicts surfaced to user (tray notification), not silently ignored |
| HOTK-03 | Launcher dismisses on Escape, on launch (instantly, no animation wait), and on click-away |
| HOTK-04 | Launcher does not steal focus from exclusive fullscreen games/windows |

## ROADMAP Success Criteria (what must be TRUE)

1. Pressing Alt+Space from any application toggles the launcher — shows when hidden, hides when visible
2. On first hotkey show, user can type immediately — zero clicks (ordered show→raise→requestActivate, deferred off the WM_HOTKEY path)
3. User can dismiss with Escape, click-outside, and via launch — launch dismissal instant, no animation wait
4. If Alt+Space is already registered by another app, user sees a tray notification with a path to change the hotkey; the newly configured hotkey re-registers and works
5. Hotkey while an exclusive-fullscreen game is active does not minimize the game or steal focus (SHQueryUserNotificationState guard)

## Locked Decisions (D-02.x — from research, non-negotiable)

- **D-02.1 (resident):** App becomes resident after first launch — start hidden, hotkey toggles visibility; Escape/click-away hide (NO quit — supersedes Phase-1 D-08); Quit only via tray menu.
- **D-02.2 (tray required):** `Qt6::Widgets` + `QApplication` conversion in `main.cpp` (QGuiApplication insufficient for QSystemTrayIcon). Minimal menu: Open wisp / Change hotkey… / Quit.
- **D-02.3 (fullscreen guard):** Defer show when QUNS is BUSY/RUNNING_D3D_FULL_SCREEN/PRESENTATION_MODE; the hotkey while fullscreen is a silent no-op — launcher NOT shown, game keeps focus.
- **D-02.4 (dismissal):** Deactivation + ~150ms grace timer (NO WS_EX_NOACTIVATE); Escape → animated dismiss → window hidden, process resident; `hideNow()` = instant no-animation hide (launch dismissal, Phase-3 consumed, proven by controller unit test this phase).
- **D-02.5 (settings):** QSettings `IniFormat` at `%APPDATA%\TID\wisp\wisp.ini` (org TID, app wisp), key `hotkey/sequence`, default `"Alt+Space"`. Chosen for testability (temp-file QSettings in tests) + clean-machine (no registry writes).
- **D-02.6 (change path):** Tray → Change hotkey… → QML capture dialog (Theme-driven) → validate (reject F12/modifier-only) → `HotkeyManager::setHotkey` re-registers immediately.

## Focus Sequence (locked in STATE.md)

show → raise → `requestActivate()` deferred OFF the WM_HOTKEY handler (QueuedConnection / singleShot(0)); `QAbstractNativeEventFilter` must return immediately. Never re-activate after show. QML: `forceActiveFocus` when window becomes active.

## Interface Contracts (created in plan 02-01, consumed downstream)

See 02-RESEARCH.md §6 — `WinHotkey`, `WinFullscreenGuard`, `HotkeyManager` (exact signatures there). Ground rule for tests/registration: use `Ctrl+Alt+F9` as the dev/CI combo (never Alt+Space); conflict paths tested by in-process double-registration.

## Non-Goals (out of scope this phase)

Search UI (Phase 3), file search (Phase 4), theming/accent picker (Phase 5), settings *window*/autostart/single-instance (Phase 6 — capture dialog here is minimal and tray-accessible). UI hint: no → no UI-SPEC this phase.

## Validation Strategy

Qt Test (Qt6::Test): `tst_hotkey` (plan 01), `tst_launcher` (plan 02), `tst_tray`+`tst_capture` (plan 03). Manual (documented in 02-VALIDATION.md): real Alt+Space vs exclusive-fullscreen game, real external conflict, tray balloon click-path. Quick run: `ctest --test-dir build/dev --output-on-failure`.