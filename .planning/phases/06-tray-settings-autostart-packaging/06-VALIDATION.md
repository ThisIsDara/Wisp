---
phase: 6
slug: tray-settings-autostart-packaging
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-08-11
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Qt Test (Qt6::Test) via CTest |
| **Config file** | CMakeLists.txt (BUILD_TESTING block) |
| **Quick run command** | `cmake --build build/dev --target wisp && ctest --test-dir build/dev --output-on-failure` |
| **Full suite command** | `cmake --build build/dev && ctest --test-dir build/dev --output-on-failure` |
| **Estimated runtime** | ~20-40 seconds |

---

## Sampling Rate

- **After every task commit:** Run targeted `ctest -R <tst_name>` for the touched test, plus the full build
- **After every plan wave:** Run `cmake --build build/dev && ctest --test-dir build/dev --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 40 seconds

---

## Per-task Verification Map

| task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 06-01-01 | 01 | 1 | SYS-01 | T-06-01 | mutex acquire/reject semantics, session-local names | unit | `ctest -R tst_singleinstance` | ❌ W0 | ⬜ pending |
| 06-01-02 | 01 | 1 | SYS-02 | T-06-02 | quoted value `"exe" --autostart`, HKCU only, scoped-key testability | unit | `ctest -R tst_autostart` | ❌ W0 | ⬜ pending |
| 06-02-01 | 02 | 2 | VISU-03 | — | token-only (no literals in new QML) | grep gate | `grep -cE "#[0-9A-Fa-f]{6}" qml/SettingsWindow.qml qml/ColorDialog.qml` == 0 | ❌ W0 | ⬜ pending |
| 06-02-02 | 02 | 2 | VISU-03 | — | danger tokenized (no `#E5484D` literal in HotkeyCaptureDialog.qml) | grep gate | `grep -c "E5484D" qml/HotkeyCaptureDialog.qml` == 0 | ✅ | ⬜ pending |
| 06-02-03 | 02 | 2 | SYS-03 | — | QML module builds (qmlcachegen passes) | build | `cmake --build build/dev --target wisp` | ✅ | ⬜ pending |
| 06-03-01 | 03 | 3 | SYS-01 | T-06-01 | tray menu order locked, settings signal, accent setter repaint | unit | `ctest -R tst_tray` | ✅ | ⬜ pending |
| 06-03-02 | 03 | 3 | SYS-03 | — | controller open/dismiss API, launcher exemption | build + regression | `cmake --build build/dev && ctest -R "tst_capture\|tst_settings"` | ✅ | ⬜ pending |
| 06-04-01 | 04 | 4 | SYS-01, SYS-02, SYS-03 | T-06-01 | boot-order: mutex first, `--autostart` parse suppresses show | smoke (manual assist: launch twice) | `ctest -R tst_launcher` + manual second-launch check | ✅ | ⬜ pending |
| 06-05-01 | 05 | 5 | SYS-04 | T-06-03 | installer builds, per-user (RequestExecutionLevel user) | script | `packaging/build-installer.ps1` exits 0, `wisp-setup.exe` exists | ❌ W0 | ⬜ pending |
| 06-05-02 | 05 | 5 | SYS-04 | T-06-03 | dynamic-link proof + relink test passes | script | `packaging/verify-lgpl.ps1` exits 0 | ❌ W0 | ⬜ pending |
| 06-05-03 | 05 | 5 | SYS-04 | — | runbook exists, THIRD-PARTY-NOTICES ships | grep gate | `grep -q "LGPL" packaging/THIRD-PARTY-NOTICES.txt` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/tst_singleinstance.cpp` — mutex acquire/reject + event signaling seam (SYS-01)
- [ ] `tests/tst_autostart.cpp` — Run-key write/remove/state via injected scoped registry key (SYS-02)
- [ ] NSIS install — `winget install NSIS.NSIS --silent` (plan 06-05 wave 5, not user setup)
- [ ] VC_redist download — build-installer.ps1 fetches from aka.ms (plan 06-05 wave 5)

*Wave 0 = first tasks of plans 06-01 and 06-05 creating the missing test/script artifacts.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Second instance surfaces existing launcher | SYS-01 | Requires two live processes + visual check | Run wisp, run wisp again — launcher window appears, no second tray icon |
| Autostart survives sign-out/sign-in | SYS-02 | Requires OS session cycle | Enable toggle, sign out/in, launcher runs quiet to tray, hotkey works |
| Settings window dismissal semantics (Esc / click-away / launcher pop-over) | SYS-03 | Visual/interactive | Open settings, press Esc (hides); click away (hides after grace); open launcher via hotkey (settings stays) |
| Accent applies live to selection/match highlighting | VISU-03 | Visual | Click swatches in settings while launcher open — chips/bars update instantly |
| Clean-VM installer run (Win10 22H2 + Win11 24H2) | SYS-04 | Requires VM snapshots (D-16) | Install → launch → hotkey → launch app → search file per `packaging/VM-RUNBOOK.md` |
| Hotkey-conflict notification with another launcher owning Alt+Space | SYS-04 | Requires second app + tray balloon | Register Alt+Space in another launcher, capture in wisp — balloon + red labels appear |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 40s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
