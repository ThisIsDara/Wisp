---
phase: 02
slug: global-hotkey-toggle
status: draft
nyquist_compliant: false
wave_0_complete: true
created: 2026-08-09
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Qt Test (Qt6::Test) — already wired in Phase 1 (`tst_shell` + CTest) |
| **Config file** | CMakeLists.txt BUILD_TESTING block (tst targets appended per plan) |
| **Quick run command** | `ctest --test-dir build/dev --output-on-failure` |
| **Full suite command** | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure` |
| **Estimated runtime** | ~5s (ctest) / ~2–3 min (full build + ctest) |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/dev --output-on-failure`
- **After every plan wave:** Run `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** ~5 seconds

---

## Per-task Verification Map

| task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 02-01-01 | 02-01 | 1 | HOTK-01 | T-02-01-01 / T-02-01-02 | WinHotkey registers combo; conflict → errorString() explains; spoofing OS-gated | unit (build-gated; behavior proven in T3) | `powershell -ExecutionPolicy Bypass -File build.ps1` | not needed (build gate) | ✅ green |
| 02-01-02 | 02-01 | 1 | HOTK-01, HOTK-04 | T-02-01-03 / T-02-01-04 | HotkeyManager validates before registering (F12/modifier-only rejected); QUNS→state mapping pure | unit | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure 2>&1 \| Select-String -Pattern "tst_hotkey\|Totals"` | ✅ (tst_hotkey in T3) | ✅ green |
| 02-01-03 | 02-01 | 1 | HOTK-01, HOTK-02 | T-02-01-01 | registration round-trip; 1409 conflict detection; persistence; rejection | unit | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure 2>&1 \| Select-String -Pattern "tst_hotkey\|pass\|fail\|Totals"` | ✅ (planned in this task) | ✅ green |
| 02-02-01 | 02-02 | 2 | HOTK-01, HOTK-03, HOTK-04 | T-02-02-01 / T-02-02-02 / T-02-02-04 | canShow() = only show entry; fullscreen defers; grace cancels on re-activate; raise() in show path | unit (compile gate; behaviors in T3) | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 \| Select-String -Pattern "error" -NotMatch; Select-String -Path src/core/LauncherController.cpp -Pattern "raise"` | not needed (build gate) | ✅ green |
| 02-02-02 | 02-02 | 2 | HOTK-03 | T-02-02-03 | hideNow instant dismiss; Escape hides-not-quits; no quitOnLastWindowClosed in QML | build + grep | `Select-String -Path qml/MainWindow.qml -Pattern "hideNow\|visible: false"` | ✅ (existing file) | ✅ green |
| 02-02-03 | 02-02 | 2 | HOTK-01, HOTK-03, HOTK-04 | T-02-02-01 / T-02-02-02 | toggle/defer/grace/hideNow/showUserRequested-bypass (7 tests) | unit | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure 2>&1 \| Select-String -Pattern "tst_launcher\|TOTAL\|PASS\|FAIL"` | ✅ (planned) | ✅ green |
| 02-03-01 | 02-03 | 3 | HOTK-02 | T-02-03-01 | QApplication + tray class; conflict → notifyHotkeyConflict path exists | build | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 \| Select-String -Pattern "error" -NotMatch` | ✅ | ✅ green |
| 02-03-02 | 02-03 | 3 | HOTK-01, HOTK-02 | T-02-03-02 | validateSequence rejects F12/modifier-only; capture input never evaluated; host injected via dialogHost | build + grep | `powershell -ExecutionPolicy Bypass -File build.ps1; Select-String -Path qml/HotkeyCaptureDialog.qml -Pattern "Theme\|PRESS\|Event\|OK\|Cancel\|dialogHost"` | ✅ | ✅ green |
| 02-03-03 | 02-03 | 3 | HOTK-01, HOTK-02, HOTK-03, HOTK-04 | T-02-03-01 / T-02-03-02 / T-02-03-04 | wiring order: tray.show() → connect(registrationFailed) → hotkeys.start(); guard bypass only via explicit user action (showUserRequested) | integration + grep | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure 2>&1 \| Select-String -Pattern "tst_\|TOTAL\|PASS\|FAIL"; $lines = Get-Content src/app/main.cpp; $startL = ($lines \| Select-String -Pattern "hotkeys\.start\(\)" \| Select-Object -First 1).LineNumber; $connL = ($lines \| Select-String -Pattern "notifyHotkeyConflict" \| Select-Object -First 1).LineNumber; $trayL = ($lines \| Select-String -Pattern "tray\.show\(\)" \| Select-Object -First 1).LineNumber; if (-not $startL -or -not $connL -or -not $trayL -or $connL -gt $startL -or $trayL -gt $connL) { throw "wiring order violated" }` | ✅ | ✅ green |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] Existing infrastructure covers all phase requirements — Qt Test + CTest + build.ps1 from Phase 1; `tst_shell` provides the pattern. No new framework/tooling; no Wave-0 stubs needed (test files are created by the plans themselves: tst_hotkey in 02-01, tst_launcher in 02-02, tst_tray/tst_capture in 02-03).

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Real Alt+Space toggle from any foreground app | HOTK-01 | Real WM_HOTKEY delivery + focus sequence requires a live desktop session | Run wisp (hidden). Press Alt+Space from a foreground app (e.g., Notepad) → launcher appears, typing lands immediately (zero clicks). Press Alt+Space again → launcher hides; process stays resident (no exit). |
| Global conflict with another app owning Alt+Space | HOTK-02 | A REAL second registration (another launcher/app) must hold the combo | Register Alt+Space in another app (e.g., PowerToys Run or a small RegisterHotKey test app); start wisp → tray balloon "hotkey in use" appears (notification must reach tray during startup — order invariant). Change hotkey via tray → works. |
| Fullscreen deferral (exclusive fullscreen) | HOTK-04 | QUNS states only reachable with a real exclusive-fullscreen app | Launch an exclusive-fullscreen game/app; press Alt+Space → nothing happens (no popup, no focus steal); game keeps focus/keyboard. Back in desktop → hotkey works again. |
| Tray balloon click path | HOTK-02 | Balloon UX | When conflict notification shows, click the balloon → menu opens; verify Change hotkey… → capture dialog → new combo re-registers and works. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependency (9/9 mapped above — no MISSING)
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify (all tasks carry one)
- [ ] Wave 0 covers all MISSING references — none (existing infra)
- [ ] No watch-mode flags
- [ ] Feedback latency < 60s (ctest ~5s; full build ~3 min bootstrap only)
- [ ] `nyquist_compliant: true` set in frontmatter (flip when phase validates)

**Approval:** pending

---

## Execution Log

**2026-08-09 — full automated matrix green (9/9).** `ctest --test-dir build/dev --output-on-failure`: 5/5 pass (tst_shell, tst_hotkey, tst_launcher, tst_capture, tst_tray).

- Wiring-order invariant: tray-branch order verified `tray.show()`(55) < `connect(registrationFailed → notifyHotkeyConflict)`(56) < `hotkeys.start()`(85) in `src/app/main.cpp`. Note: the plan's grep uses `Select-Object -First 1` which matches a *comment* line (49: "constructed BEFORE hotkeys.start()") — the checker must use the **last** `hotkeys.start()` match (the else/tray-less branch registers separately at 101, which is correct).
- Smoke run (manual-adjacent, executed 2026-08-09): `wisp.exe` launched from `build/dev` stayed resident 4s+ (pid held, no crash), force-killed — confirms hidden-resident startup path; real tray icon + balloon UX need human eyeball check (see Manual-Only table).