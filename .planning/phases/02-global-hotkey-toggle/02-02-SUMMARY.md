---
phase: 02-global-hotkey-toggle
plan: 02
subsystem: controller-policy, resident-window, deactivation-grace
tags: [qt6, qml, qquickwindow, notify, resident]

requires:
  - phase: 02-global-hotkey-toggle
    provides: plan 01 (WinFullscreenGuard::State + guard contract)
provides:
  - LauncherController: toggle state machine with guard-checked show (HOTK-04 defer), showUserRequested (D-02.3 bypass), 150ms deactivation grace (D-02.4 click-away), hideAnimated vs hideNow instant dismiss (HOTK-03), locked focus sequence show→raise→deferred-requestActivate
  - MainWindow.qml: resident lifecycle (visible:false, Escape animated-hides without quit, hideNow(), QML focus hand-off, closing-flag reset per hide)
  - tst_launcher: 7-test window-light suite (fake guard lambda) — ctest 3/3 green
affects: [02-global-hotkey-toggle (plan 03), 03, 06]

tech-stack:
  added: [Qt6::Quick as PRIVATE dep of wisp_core (QQuickWindow in LauncherController.cpp)]
  patterns: [C++ controller owns all visibility policy (no QML logic), window-light testability via null-window + injectable guard]

key-files:
  created: [src/core/LauncherController.h, src/core/LauncherController.cpp, tests/tst_launcher.cpp]
  modified: [qml/MainWindow.qml, CMakeLists.txt]

key-decisions:
  - "All window interaction via QPointer<QQuickWindow> + QMetaObject::invokeMethod (dismiss/hideNow/requestActivate) on the QML object — no direct C++/QML coupling"
  - "hideNow() routes through the QML hideNow() (resets closing flag + stops close animation) with win->hide() fallback — single source of truth for instant dismissal"
  - "Focus sequence: show→raise→invokeMethod(requestActivate, QueuedConnection); if name resolution fails, fallback QTimer::singleShot(0, win, &QWindow::requestActivate) — still deferred off the hotkey path"
  - "Deactivation grace timeout hides only when the window is still inactive (or null in tests) — focus re-grant during grace is never force-hid (T-02-02-02)"
  - "MainWindow: closing flag resets in onVisibleChanged(!visible) — REQUIRED for residence: without it a second Escape after the first hide would be swallowed by the close lock (Phase-1 quit-on-close never hit this)"
  - "Grace cancellation: onWindowActiveChanged(true) stops the timer; timeout path checks m_state==Visible first"

patterns-established:
  - "New src/core/* classes must be registered in the wisp_core source list (LauncherController was forgotten at first → 8 LNK2019s in tst_launcher)"
  - "Run ctest/build via build.ps1 (vcvars env); direct cmake --build without vcvars fails on d3d11.lib (LNK1104) — SDK lib paths come from the vcvars64.bat environment"

requirements-completed: [HOTK-01, HOTK-03]

# Metrics
duration: 40min
completed: 2026-08-09
---

# Phase 02 Plan 02: LauncherController + Resident Shell Summary

The launcher now owns its visibility lifecycle in a unit-testable C++ controller: hotkey toggle with fullscreen defer, deactivation-based click-away with 150ms grace, animated vs instant dismissal, and a resident window that starts hidden and never quits on close. All policy verified window-light — ctest 3/3 (tst_shell, tst_hotkey, tst_launcher).

## Performance

- **Duration:** ~40 min
- **Completed:** 2026-08-09
- **Tasks:** 3 (controller / QML resident lifecycle / tests + wiring)
- **Files modified:** 5 (per plan)

## Task Status

| # | Task | Commits | Status |
|---|------|---------|--------|
| 1 | LauncherController state machine | `3e40d10` | Done |
| 2 | MainWindow.qml resident lifecycle | (in `3e40d10`+QML edits, committed with task 1) | Done |
| 3 | tst_launcher 7 tests + wisp_core wiring | `cc43560` | Done |

## Verification

- Build clean; `ctest --test-dir build/dev`: **3/3 passing** (tst_shell 0.23s, tst_hotkey 0.13s, tst_launcher 0.69s)
- tst_launcher covers: toggle-show guard consulted once, fullscreen defer (HOTK-04), grace-window dismiss + re-activation cancel (D-02.4), hideNow instant + no-op-on-hidden, hideAnimated state tracking, guard-flip-while-visible hide, showUserRequested bypass (D-02.3)
- Plan greppable checks: `raise()` in LauncherController.cpp ✓; `visible: false` + `hideNow()` + `forceActiveFocus` in MainWindow.qml ✓; no `root.close()`/`Qt.quit()` in closeAnim.onFinished (comment-only mention) ✓

## Issues Encountered

1. **LauncherController.cpp missing from wisp_core** — 8 LNK2019s in tst_launcher (every controller method unresolved). Added to the wisp_core source list; incremental rebuild clean.
2. **LNK1104 d3d11.lib on bare `cmake --build`** — Qt6::Quick pulls DirectX SDK libs; the LIB paths only exist inside vcvars64.bat's environment, which build.ps1 wraps. Run all builds through build.ps1.
3. **Residence exposes a Phase-1 latent bug**: `closing` flag never reset after hide — a second Escape would be swallowed. Fixed in onVisibleChanged(!visible) (Task 2 scope — required for D-02.1).

## Notes for Downstream Plans

- 02-03 wires: `LauncherController::setWindow(engine.rootObjects().first())`, `win->activeChanged → controller.onWindowActiveChanged`, `HotkeyManager::hotkeyPressed → controller.toggle`, TrayIcon::openWisp → `showUserRequested()`.
- Phase 3 consumes `hideNow()` for launch dismissal; Phase 6 reuses showUserRequested for tray Open.
- Focus hand-off is now QML-side (`shell.forceActiveFocus()` on activeChanged) — MainWindow itself is the only QML file carrying lifecycle glue.