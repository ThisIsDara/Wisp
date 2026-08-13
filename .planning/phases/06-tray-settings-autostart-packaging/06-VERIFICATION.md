---
phase: 06-tray-settings-autostart-packaging
verified: 2026-08-11T23:45:00Z
status: gaps_found
score: 13/15 must-haves verified
overrides_applied: 0
gaps:
  - truth: "The settings hotkey row reopens the existing fullscreen HotkeyCaptureDialog; accepted sequence re-registers via HotkeyManager (D-03, success criterion 3)"
    status: failed
    reason: "CR-01 (confirmed in code): SettingsWindow.qml emits hotkeyRowClicked on mouse click (line 180) and keyboard (lines 156-158), but NOTHING is connected to it — no onHotkeyRowClicked handler in QML, no connect() in SettingsWindow.cpp, no handler anywhere in src/. The controller's Q_INVOKABLE openHotkeyCapture() (SettingsWindow.cpp:130) is never invoked. Clicking 'Hotkey / Click to change' in the settings window is a complete no-op. Roadmap SC3 'user can capture a new hotkey' via the settings window is dead."
    artifacts:
      - path: "qml/SettingsWindow.qml"
        issue: "hotkeyRowClicked signal declared (line 48) and emitted (lines 156-158, 180) with no handler"
      - path: "src/ui/SettingsWindow.cpp"
        issue: "openHotkeyCapture() defined (line 130) but never called; no connect for hotkeyRowClicked"
    missing:
      - "Wire hotkeyRowClicked → openHotkeyCapture() (connect in ensureWindow after m_window is set, or replace the signal with direct settingsController.openHotkeyCapture() calls in QML)"
  - truth: "The hotkey capture dialog can be re-opened for subsequent hotkey changes within the same session"
    status: failed
    reason: "CR-02 (confirmed in code): HotkeyCaptureDialog::open() (src/ui/HotkeyCaptureDialog.cpp:47-48) early-returns while m_dialog is non-null; the QPointer never nulls because the QML window is engine-owned and only ever hidden (accept path hides at line 81; cancel path calls root.hide()+root.close() which also just hides). Every open() after the first is a silent no-op until restart — the tray 'Change hotkey…' path (main.cpp:258-260) works exactly once per session. Roadmap SC3 're-registers and works immediately' fails for repeat captures."
    artifacts:
      - path: "src/ui/HotkeyCaptureDialog.cpp"
        issue: "if (m_dialog) return; with a QPointer that never nulls — single-use per session"
    missing:
      - "Reuse the hidden window: if (m_dialog->isVisible()) return; else setProperty(currentSequence) + show() + requestActivate() — or destroy on close (JavaScriptOwnership + deleteLater) so the QPointer nulls"
human_verification:
  - test: "Open settings from the tray, click the Hotkey row (mouse and keyboard Enter/Space) after CR-01 is fixed"
    expected: "Fullscreen capture dialog opens, capture re-registers and the well text updates"
    why_human: "Currently known-dead (grep-proven); re-test after fix — interactive behavior"
  - test: "Settings dismissal semantics: Esc hides; click-away hides after ~150ms grace; launcher hotkey pop-over keeps settings open"
    expected: "Esc = instant hide; foreign click-away = hide after grace; launcher pop-over = settings stays"
    why_human: "Visual/interactive timing behavior — cannot be verified programmatically"
  - test: "Second-instance smoke (visual): launch wisp twice"
    expected: "First instance's launcher window appears; second process exits"
    why_human: "Process-level contract verified (06-04 smoke: second exits 0, one process remains); the on-screen appearance needs eyes — recorded as pending manual step in 06-04-SUMMARY"
  - test: "Change hotkey twice in a row via tray menu (after CR-02 fix)"
    expected: "Second 'Change hotkey…' opens the dialog again with current combo prefilled"
    why_human: "Currently fails by code inspection (QPointer never nulls); re-test after fix"
  - test: "Autostart sign-out/sign-in cycle"
    expected: "After enabling 'Start with Windows', sign out/in → wisp boots quiet to tray, hotkey works"
    why_human: "Requires an OS session cycle — cannot be automated"
  - test: "Accent applies live to selection and match highlighting while launcher is open"
    expected: "Clicking swatches in settings updates chips/bars instantly (Phase-5 reactive path)"
    why_human: "Visual appearance — per 06-VALIDATION.md manual checklist"
  - test: "VM-RUNBOOK step 7 re-run on a clean VM after CR-01/CR-02 are fixed"
    expected: "Settings → Hotkey row opens capture; conflict with PowerToys Run owning Alt+Space produces the balloon"
    why_human: "The recorded user-approved PASS for step 7 is not reproducible from the current code (settings hotkey row is dead); the runbook's 'red conflict labels' claim is additionally unsupported (WR-02: showValidationError is declared on a child Text, not the root window)"
---

# Phase 6: Tray, Settings, Autostart & Packaging Verification Report

**Phase Goal:** The app becomes a resident citizen: tray icon with Open/Settings/Quit and single-instance, settings window (hotkey capture, accent color picker, autostart toggle), start-with-Windows, and a clean-machine installer with LGPL compliance verified — the release gate.
**Verified:** 2026-08-11T23:45:00Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

The resident foundation, tray surface, settings surface (accent + autostart), boot wiring, and the release-gate packaging pipeline are all real, substantive, wired, and test-backed (19/19 ctest green; installer built; LGPL relink spot-check passed against the deployed DLLs). **However, the phase's flagship settings-window interaction — hotkey capture — is functionally dead**: the settings hotkey row is wired to nothing (CR-01), and the capture dialog can be opened only once per session (CR-02). Both are code-confirmed, unfixed, and match the 06-REVIEW.md critical findings. Roadmap SC3 is not achieved.

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Tray icon present with locked menu order Open wisp / Settings / Change hotkey… / sep / Quit; Settings emits a signal | ✓ VERIFIED | TrayIcon.cpp:52-64 menu construction; tst_tray.cpp:34-36,73-75 asserts order + signal spy; main.cpp:266-267 connects settingsRequested → SettingsWindow::open |
| 2 | Second instance detects the running instance, signals it, and exits instead of duplicating | ✓ VERIFIED | main.cpp:41-48 guard-first; WinSingleInstance.cpp CreateMutexW + ERROR_ALREADY_EXISTS path; tst_singleinstance.cpp:30-54; 06-04 smoke (second exits 0, first survives) |
| 3 | Second instance surfaces the existing instance (showRequested → launcher) | ✓ VERIFIED | main.cpp:216-218 connect + startWatching; WinSingleInstance.cpp CreateEventW/SetEvent watcher; tst_singleinstance event round-trip; visual half → human item |
| 4 | Autostart toggle writes/removes the quoted HKCU Run key with exact `"<exe>" --autostart` value | ✓ VERIFIED | AutostartManager.cpp:47-52 D-12 construction; tst_autostart.cpp initiallyDisabled/enableWritesQuotedValue/disableRemovesValue with disk readback; 19/19 green |
| 5 | Settings window opens as the locked 480x360 dark tool window with Hotkey / Accent color / Start with Windows rows | ✓ VERIFIED | SettingsWindow.qml:14-21 Window + three rows; Theme tokens settingsWindowWidth/Height; zero hex literals |
| 6 | Clicking a swatch applies the accent live; Custom… opens the staged color dialog; commit persists | ✓ VERIFIED | SettingsWindow.qml:253 applySwatch → controller.applyAccent → SettingsStore::setAccent; ColorDialog.qml:62-67 commitCustomColor; SettingsWindow.cpp:101-118; live apply via Phase-5 accentChanged path |
| 7 | All colors/spacings come from Theme tokens; 9-swatch accentSwatches array; HotkeyCaptureDialog literals tokenized | ✓ VERIFIED | Theme.qml:99 accentSwatches (9 locked hexes), :121 toggleTrackOn binding, :130 danger; HotkeyCaptureDialog.qml uses Theme.danger/fieldRadius/fieldHeight/onAccentText; zero hex literals in new QML |
| 8 | Tray disc is live-bound to the accent (startup + accentChanged) | ✓ VERIFIED | main.cpp:226 tray.setAccent(settingsStore.accent()); main.cpp:270-271 accentChanged → setAccent; TrayIcon.cpp:80-88 repaint + re-setIcon; tst_tray.cpp:87-104 |
| 9 | Settings window opens centered with 120ms fade; Esc/click-away close with 150ms grace and launcher exemption | ✓ VERIFIED | SettingsWindow.cpp:55-77 open sequence, 161-173 Esc filter, 199-206 activeChanged grace, 234-265 exemption scan; wiring complete — visual timing → human item |
| 10 | **Settings hotkey row reopens the capture dialog; accepted sequence re-registers via HotkeyManager** | ✗ FAILED | **CR-01**: hotkeyRowClicked emitted (SettingsWindow.qml:156-158,180) but no handler exists anywhere (repo-wide grep); openHotkeyCapture() (SettingsWindow.cpp:130) never invoked. Clicking the row does nothing |
| 11 | Capture dialog is re-openable for repeat hotkey changes in a session | ✗ FAILED | **CR-02**: HotkeyCaptureDialog.cpp:47-48 `if (m_dialog) return;` — QPointer never nulls (window hidden, never destroyed); tray Change hotkey… works once per session |
| 12 | --autostart boots quiet to tray (D-11/D-12 parse) | ✓ VERIFIED | main.cpp:69-71 exact-string parse (Q_UNUSED, documented — launcher boots resident-hidden by construction D-02.1); tst_autostart covers the value format |
| 13 | Tray Settings opens the settings window; launcher hotkey still summons the launcher | ✓ VERIFIED | main.cpp:266-267 tray-only entry; main.cpp:246-247 hotkey → toggle unchanged; no launcher affordance added (D-04) |
| 14 | Installer builds to a per-user NSIS setup (RequestExecutionLevel user, %LOCALAPPDATA%\Programs\wisp, Start Menu shortcut) — no UAC | ✓ VERIFIED | installer.nsi:12,13,33-39,47-51,63; build/deploy/wisp-setup.exe exists (73.7 MB); build-installer.ps1 pipeline (deploy → VC_redist → makensis) |
| 15 | Installer verified on clean Win10 22H2 + Win11 24H2 VMs (D-16 runbook, user-approved) | ✓ VERIFIED (caveat) | VM-RUNBOOK.md 8 steps + results table PASS × 16, user-approved 2026-08-11. **Caveat:** step 7 as written (Settings → Hotkey) cannot execute with CR-01 unfixed; the runbook's "red conflict labels" claim is unsupported (WR-02) — recorded as a human item |
| 16 | LGPL compliance verified: NOTICES ships in installer, Qt dynamically linked (relink test), source offer documented | ✓ VERIFIED | THIRD-PARTY-NOTICES.txt in deploy folder + `File /r` in installer.nsi:44; verify-lgpl.ps1 dumpbin + relink; spot-check re-run: relink-test.exe exit 0 "RELINK OK" with deploy-only PATH; dumpbin re-run shows Qt6Core/Gui/Qml/Quick/Widgets imported; LGPL-COMPLIANCE.md source offer (LGPL §6(d), 3-year) |

**Score:** 13/15 truths verified

### Deferred Items

None — Phase 6 is the final phase of the milestone; no later phase addresses the hotkey-row wiring or capture-dialog re-openability (Step 9b: no deferral).

### Required Artifacts

| Artifact | Expected | Status | Details |
| -------- | -------- | ------ | ------- |
| `src/win/WinSingleInstance.h` | tryAcquire/signalShow/startWatching/showRequested, no Win32 headers | ✓ VERIFIED | Pure interface, opaque void* handles; header comment avoids literal "Windows.h" |
| `src/win/WinSingleInstance.cpp` | CreateMutexW/CreateEventW/SetEvent watcher | ✓ VERIFIED | Local\ namespace names; create-or-open Pitfall-1 fix; std::thread + join shutdown |
| `src/core/AutostartManager.{h,cpp}` | isEnabled/setEnabled, HKCU Run key, quoted D-12 value | ✓ VERIFIED | makeRunKey factory seam; exact `"<exe>" --autostart`; sync() after mutation |
| `src/tray/TrayIcon.{h,cpp}` | Settings action, locked order, accent-aware disc | ✓ VERIFIED | 4 items + separator; settingsRequested; setAccent repaint + re-setIcon; D-16 invalid-ignore |
| `src/ui/SettingsWindow.{h,cpp}` | QML host controller: open/close/dismissal/capture handoff/store wiring | ✓ VERIFIED (except capture handoff) | Full controller present; openHotkeyCapture() exists but is **never called** (CR-01) |
| `src/ui/HotkeyCaptureDialog.cpp` | Re-openable capture dialog | ✗ FAILED | Single-use per session (CR-02) |
| `src/app/main.cpp` | Boot wiring: guard-first, --autostart, summon, tray→settings, accent→tray | ✓ VERIFIED | Lines 41-48, 69-71, 216-218, 226, 266-271; smoke verified at process level |
| `qml/SettingsWindow.qml` | 480x360 locked surface, 3 rows, 9 swatches, toggle | ✓ VERIFIED (surface) | Token-only; hotkeyRowClicked emitted but unhandled (CR-01) |
| `qml/ColorDialog.qml` | 280x320 ApplicationModal staged dialog | ✓ VERIFIED | SV square, hue bar, hex readout, OK/Cancel; commitCustomColor wired |
| `qml/Theme.qml` | 35+ tokens, accentSwatches, danger, toggleTrackOn binding | ✓ VERIFIED | Grep-verified; cyan contrast clamp check documented (no change needed) |
| `qml/HotkeyCaptureDialog.qml` | Tokenized, zero hex literals | ✓ VERIFIED | 4 literal → token replacements; WR-02: showValidationError on child Text (warning) |
| `packaging/installer.nsi` | Per-user NSIS, no UAC, VC_redist gate, locked copy | ✓ VERIFIED | RequestExecutionLevel user; HKCU uninstall keys; WR-04: uninstall leaves Run value (warning) |
| `packaging/build-installer.ps1` | deploy → VC_redist → makensis | ✓ VERIFIED | Official aka.ms URL, >1MB sanity; WR-06: no checksum (warning) |
| `packaging/verify-lgpl.ps1` | dumpbin + relink test runner | ✓ VERIFIED | Deploy-only PATH relink; WR-07: asserts 4 DLLs (Widgets/Concurrent gap — actual imports verified separately: Widgets IS imported, Concurrent is header-merged into QtCore since Qt 6.9, no separate DLL) |
| `packaging/relink-test/main.cpp` | Dynamic-link proof binary | ✓ VERIFIED | RELINK OK spot-check exit 0; WR-08: prints compile-time prefix (evidence-quality, warning) |
| `packaging/LGPL-COMPLIANCE.md` | Evidence doc + source offer | ✓ VERIFIED | Relink procedure, dynamic-link rationale, §6(d) offer |
| `packaging/VM-RUNBOOK.md` | 8-step clean-VM checklist + results | ✓ VERIFIED | User-approved PASS × 16; step-7 path contradicted by CR-01 (caveat) |
| `packaging/THIRD-PARTY-NOTICES.txt` | Qt LGPLv3 notice + source offer | ✓ VERIFIED | Ships next to wisp.exe + inside installer; WR-10: module list omits Widgets (warning) |
| `tests/tst_singleinstance.cpp` | Mutex + event channel tests | ✓ VERIFIED | acquire/reject + showRequested round-trip (13 grep hits) |
| `tests/tst_autostart.cpp` | Run-key initial/enable/disable tests | ✓ VERIFIED | Exact-value QCOMPARE + shape regex + cleanup |
| `tests/tst_tray.cpp` | 5-entry menu, settings signal, accent setter | ✓ VERIFIED | Position-verified assertions; invalid-ignore test |

### Key Link Verification

| From | To | Via | Status | Details |
| ---- | -- | --- | ------ | ------- |
| main.cpp:41-48 | WinSingleInstance::tryAcquire | boot guard first statement | ✓ WIRED | Before QApplication/window construction |
| main.cpp:216-218 | showRequested → LauncherController::showUserRequested | connect + startWatching | ✓ WIRED | Connect-then-watch; outside tray branch |
| main.cpp:266-267 | TrayIcon::settingsRequested → SettingsWindow::open | connect | ✓ WIRED | Tray-only entry (D-04) |
| main.cpp:226,270-271 | SettingsStore::accent → TrayIcon::setAccent | startup call + accentChanged lambda | ✓ WIRED | Live repaint path complete |
| SettingsWindow.qml → openHotkeyCapture | hotkeyRowClicked → capture handoff | QML signal → C++ slot | ✗ NOT_WIRED | **CR-01**: signal emitted, zero handlers; openHotkeyCapture never invoked |
| SettingsWindow.cpp:43-47 | HotkeyCaptureDialog::accepted → HotkeyManager::setHotkey | connect | ✓ WIRED | Immediate re-registration when a capture completes |
| main.cpp:258-260 | TrayIcon::changeHotkeyRequested → capture.open | connect | ⚠️ PARTIAL | Wired, but capture.open works only once per session (CR-02) |
| AutostartManager.cpp:16-19 | HKCU Run key via QSettings NativeFormat | makeRunKey factory | ✓ WIRED | `HKEY_CURRENT_USER\...\CurrentVersion\Run` |
| SettingsWindow.cpp:141,181 | ColorDialog.qml / SettingsWindow.qml | QQmlComponent load | ✓ WIRED | qrc:/qt/qml/wisp/..., beginCreate/setProperty injection |
| build-installer.ps1 → installer.nsi → deploy folder | makensis + File /r build/deploy/wisp | pipeline | ✓ WIRED | wisp-setup.exe 73.7 MB produced |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| -------- | ------------- | ------ | ------------------ | ------ |
| SettingsWindow.qml hotkey well | currentHotkey | HotkeyManager::hotkey() (live Q_PROPERTY + NOTIFY) | Yes — real persisted combo | ✓ FLOWING |
| SettingsWindow.qml swatch strip | Theme.accentSwatches + applyAccent | 9 locked hex tokens → SettingsStore::setAccent → accentChanged → Theme.accent | Yes — real persist + live apply | ✓ FLOWING |
| SettingsWindow.qml toggle | autostartEnabled | AutostartManager::isEnabled() (live read; outcome-reflecting refresh after toggle) | Yes — real registry state | ✓ FLOWING |
| ColorDialog.qml hexReadout | staged HSV → hsvToHex → commitCustomColor | Generated from staged state, not freeform | Yes — but WR-01: achromatic accent seeds stageHue=-1 → commits wrong hue | ⚠️ FLOWING (edge-case wrong color) |
| TrayIcon disc | m_accent | setAccent from SettingsStore (startup + accentChanged) | Yes — real accent | ✓ FLOWING |
| HotkeyCaptureDialog capture path | capturedSequence → submitSequence → accepted → setHotkey | Real key events; validation F12/mod-only | Yes — but WR-02: invalid-sequence red label invokeMethod targets root, function is on child Text → dead feedback path; CR-02: single-use | ⚠️ FLOWING (broken edges) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| Full test suite | `ctest --test-dir build/dev --output-on-failure` | 19/19 passed, 0 failures | ✓ PASS |
| Installer exists | `Test-Path build/deploy/wisp-setup.exe` | True, 73,743,753 bytes | ✓ PASS |
| Deploy folder complete | `Test-Path build/deploy/wisp/{wisp.exe,THIRD-PARTY-NOTICES.txt}` | Both True | ✓ PASS |
| LGPL relink evidence | relink-test.exe with PATH=deploy-only | exit 0, "RELINK OK - Qt6Core resolved from: build/deploy/lib" | ✓ PASS (note: printed path is the compile-time prefix — WR-08; loaded DLL is genuinely the deployed one) |
| Qt dynamic imports | dumpbin /DEPENDENTS on deployed wisp.exe | Qt6Core/Gui/Qml/Quick/Widgets: True; Qt6Concurrent: absent (header-merged into QtCore since Qt 6.9 — expected, no separate DLL) | ✓ PASS |
| Settings hotkey row handler | repo-wide grep `hotkeyRowClicked` | Only declaration + 4 emitters in SettingsWindow.qml; zero handlers | ✗ FAIL (CR-01) |
| Capture dialog re-open | code trace of `HotkeyCaptureDialog::open()` | `if (m_dialog) return;` — QPointer never nulls | ✗ FAIL (CR-02) |

### Requirements Coverage

| Requirement | Source Plan(s) | Description (REQUIREMENTS.md) | Status | Evidence |
| ----------- | -------------- | ----------------------------- | ------ | -------- |
| SYS-01 | 06-01, 06-03, 06-04 | System tray icon with Open / Settings / Quit menu; single-instance enforcement | ✓ SATISFIED | TrayIcon menu + settingsRequested; WinSingleInstance guard + show channel; main.cpp guard-first; tst_singleinstance + tst_tray |
| SYS-02 | 06-01, 06-04 | User can toggle "start with Windows" (autostart) in settings | ✓ SATISFIED | SettingsWindow toggle → AutostartManager → quoted HKCU Run key `"<exe>" --autostart`; tst_autostart |
| SYS-03 | 06-02, 06-03, 06-04 | Settings window with hotkey capture, accent color picker, and autostart toggle | ✗ BLOCKED | Accent picker ✓ and autostart toggle ✓; **hotkey capture from settings window is dead (CR-01)** and capture is single-use per session (CR-02) |
| SYS-04 | 06-05 | Installer works on clean Windows 10/11 machines (NSIS, windeployqt --qmldir, VC redist) with Qt LGPL notices included | ✓ SATISFIED | Per-user NSIS installer built; user-approved clean-VM runs (step-7 caveat); NOTICES ships; relink + dumpbin evidence; source offer documented |
| VISU-03 | 06-02 | User can pick an accent color used for selection and match highlighting | ✓ SATISFIED | 9-swatch strip + custom ColorDialog → SettingsStore::setAccent → live apply (Phase-5 reactive path); WR-01 achromatic edge-case noted |

**Traceability:** All 5 requirement IDs claimed by PLAN frontmatters (SYS-01, SYS-02, SYS-03, SYS-04, VISU-03) exist in REQUIREMENTS.md and are mapped to Phase 6 in ROADMAP.md's coverage table. No orphaned requirements.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| qml/SettingsWindow.qml | 48,156-158,180 | Dead signal (emitted, never handled) | 🛑 Blocker | Settings hotkey row does nothing (CR-01) |
| src/ui/HotkeyCaptureDialog.cpp | 47-48 | Guard on QPointer that never nulls | 🛑 Blocker | Capture dialog single-use per session (CR-02) |
| qml/ColorDialog.qml | 83 | Unclamped `Theme.accent.hsvHue` (-1 for achromatic) | ⚠️ Warning | Commits a wrong hue after a grey accent was picked (WR-01) |
| src/ui/HotkeyCaptureDialog.cpp | 73-76 | invokeMethod target wrong object (function on child Text) | ⚠️ Warning | F12/mod-only rejection shows no red label (WR-02) |
| src/app/main.cpp | 161-164 | Context property set after loadFromModule | ⚠️ Warning | Latent binding break risk (WR-03) |
| packaging/installer.nsi | 67-71 | Uninstall leaves HKCU Run value | ⚠️ Warning | Stale autostart entry after uninstall/reinstall (WR-04) |
| packaging/installer.nsi | 55-58 | VC_redist /quiet requires elevation; ExecWait unchecked | ⚠️ Warning | UAC prompt contradicts runbook claim; silent runtime failure if redist fails (WR-05) |
| packaging/build-installer.ps1 | 35-41 | No checksum/Authenticode on redist download | ⚠️ Warning | Supply-chain integrity gap (WR-06) |
| packaging/verify-lgpl.ps1 | 30-33 | Gate asserts 4 Qt DLLs | ℹ️ Info | Widgets verified imported via separate check; Concurrent has no separate DLL in Qt 6.11 (header-merged) (WR-07) |
| packaging/relink-test/main.cpp | 19-20 | Prints compile-time prefix as "evidence" | ⚠️ Warning | Misleading log line (WR-08) |
| packaging/THIRD-PARTY-NOTICES.txt | 18-26 | Module list omits Qt Widgets | ⚠️ Warning | Understated compliance artifact (WR-10) |

### Human Verification Required

1. **Settings hotkey row capture** — open Settings → click "Hotkey / Click to change" (mouse + Enter/Space). Currently known-dead (CR-01); re-test after fix. Expected: fullscreen capture dialog opens, new combo re-registers and works immediately.
2. **Settings dismissal semantics** — Esc hides instantly; click-away hides after ~150ms grace; launcher hotkey pop-over keeps settings open. Visual/interactive timing.
3. **Second-instance visual summon** — launch wisp twice; first instance's launcher window must appear. Process-level contract verified; the on-screen appearance needs eyes (pending step from 06-04).
4. **Repeat hotkey change** — tray → Change hotkey… twice in a row after CR-02 fix. Expected: second open works with current combo prefilled.
5. **Autostart sign-out/sign-in** — enable toggle, sign out/in; wisp boots quiet to tray and the hotkey works. Requires an OS session cycle.
6. **Accent live apply visual** — click swatches in settings with the launcher open; selection/match highlighting updates instantly.
7. **VM-RUNBOOK step 7 re-run after CR-01/CR-02 fixes** — the recorded PASS is not reproducible from the current code (settings hotkey row dead; "red conflict labels" unsupported per WR-02). The balloon path itself (registrationFailed → tray) is code-verified.

### Gaps Summary

Two blocker gaps prevent the phase goal from being fully achieved — both are the 06-REVIEW.md critical findings, both confirmed in code, neither fixed in any commit after the review:

1. **CR-01 — Settings hotkey row is dead.** `hotkeyRowClicked` is emitted by mouse and keyboard paths in SettingsWindow.qml but has no handler anywhere in the codebase; `SettingsWindow::openHotkeyCapture()` is never invoked. The D-03 "hotkey-row handoff" — the flagship interaction of the phase's settings window — does nothing. Roadmap SC3 ("user can capture a new hotkey… re-registers and works immediately") and 06-03 must-have #4 fail. The only working capture entry is the tray menu (and only once — see CR-02).

2. **CR-02 — Capture dialog is single-use per session.** `HotkeyCaptureDialog::open()` early-returns on a QPointer that never nulls (the QML window is engine-owned and only hidden on accept/cancel). After the first hotkey change, every subsequent "Change hotkey…" click is a silent no-op until restart. Combined with CR-01, the hotkey-capture feature of the phase is effectively non-functional beyond a single first use via the tray.

All other must-haves (tray, single-instance, autostart, settings surface, accent picker, boot wiring, installer, LGPL evidence, clean-VM approval) are verified working. The 19/19 test suite passes but contains no test exercising the hotkey-row handoff or capture-dialog re-open — the gaps are invisible to the suite.

Fix suggestions (from 06-REVIEW.md): connect `hotkeyRowClicked` → `openHotkeyCapture()` in `SettingsWindow::ensureWindow()` or call `settingsController.openHotkeyCapture()` directly in QML; and for CR-02 reuse the hidden window (`if (m_dialog->isVisible()) return; … show()`) or destroy on close so the QPointer nulls.

---

_Verified: 2026-08-11T23:45:00Z_
_Verifier: OpenCode (gsd-verifier)_
