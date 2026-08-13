# Phase 6: Tray, Settings, Autostart & Packaging - Research

**Researched:** 2026-08-11
**Domain:** Windows resident-app integration (single-instance, HKCU autostart, QML settings surface, NSIS packaging, LGPL compliance)
**Confidence:** HIGH

## Summary

Phase 6 turns wisp into a resident citizen: tray menu restructure (Settings action), single-instance enforcement, an autostart toggle writing the quoted HKCU Run key, a settings window (hotkey row + accent picker + autostart toggle), and a per-user NSIS installer with LGPL compliance verified. The phase is almost entirely **assembly of already-verified patterns**: every UI value is locked by the approved 06-UI-SPEC.md, the Win32 APIs were verified in project research (STACK.md), and the existing codebase (TrayIcon, HotkeyCaptureDialog, SettingsStore, main.cpp boot order) provides direct analogs.

Three environment facts shape the plans: **NSIS is not installed** but is automatable via `winget install NSIS.NSIS` (3.12 available — no user setup needed); **VS2022 Community's dumpbin** is present (relink/dynamic-link verification tool); **VC_redist runtime is installed on the dev machine** (registry check pattern confirmed) but must be downloaded at build time for clean-VM installs. The single-instance design uses `CreateMutexW` + a named event channel per D-09 — both standard Win32, verifiable in `tst_*` via same-process duplicate acquisition.

**Primary recommendation:** 5 sequential plans (shared CMakeLists.txt forces wave ordering): foundation (WinSingleInstance + AutostartManager) → QML surfaces (Theme tokens, SettingsWindow.qml, ColorDialog.qml) → tray restructure + settings controller → main.cpp wiring → installer + LGPL. Each plan owns its CMakeLists.txt edits, matching the Phase 4/5 repo convention.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Single-instance (mutex + signal) | Client (C++ `src/win/`) | — | Win32 detail behind the `src/win/` firewall, pure interface, unit-testable (Phase-5 pattern) |
| Autostart Run-key state | Client (C++ `src/core/`) | — | QSettings NativeFormat is a registry wrapper; no Win32 needed → `wisp_core` policy store |
| Settings window UI | Client (QML `qml/`) | Client controller (`src/ui/`) | HotkeyCaptureDialog analog: thin C++ host + token-only QML |
| Accent picker persistence | Client (`SettingsStore`) | — | Reuses Phase-5 setAccent → accentChanged → Theme.accent; zero new persistence |
| Tray menu + icon | Client (`src/tray/`) | — | QSystemTrayIcon in Widgets; accent via setter (no SettingsStore reach-in) |
| Boot wiring | Client (`src/app/main.cpp`) | — | Single-instance check first, `--autostart` parse, tray→settings connect |
| Installer | Dev-tooling (NSIS) | Client deploy (`deploy.ps1`) | NSIS consumes windeployqt output; dev-time only, not a runtime surface |
| LGPL verification | Dev-tooling (dumpbin + relink test) | — | Proof artifact in `packaging/` |

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Settings = a **dedicated dark QML window** (new `SettingsWindow.qml`, built like MainWindow.qml: Theme tokens, concrete widgets, no QQC) backed by a small C++ controller. NOT an extension of HotkeyCaptureDialog, NOT Qt Widgets.
- **D-02:** Window shape = **small themed tool window** (~480×360), non-modal, auto-centered on open, closes on Esc / click-away — same dismissal semantics as the launcher. No tabbed layout.
- **D-03:** Tray menu gains **Settings** ("Open wisp / Settings / Quit") AND **keeps "Change hotkey…"** — both paths stay: the tray item opens the existing fullscreen capture dialog directly; the settings window's hotkey row also reopens that same fullscreen `HotkeyCaptureDialog` when clicked (reuses the validated capture flow and `validateSequence`).
- **D-04:** The settings window opens from the tray (Settings item) only; the launcher window itself gains no settings affordance. Non-modal so the launcher can still pop over it.
- **D-05:** Picker = **row of curated swatches (8–10 handpicked colors tuned for the dark theme) + a custom option**. No Windows-11 palette dump, no standalone wheel.
- **D-06:** Accent changes **apply live and persist on change** — the Phase-5 wiring already delivers this (SettingsStore::setAccent → accentChanged → Theme.accent); selection background, left bar, match-highlight chips all update instantly. No Save/Apply button.
- **D-07:** Custom option opens a **custom dark-themed QML color dialog** (not the native QColorDialog) — consistent with the app aesthetic; picks hue/saturation/value in the same token language.
- **D-08:** The 8–10 swatch values live in Theme.qml as tokens (token-only rule); the "custom" color is stored via the existing `setAccent`.
- **D-09:** Single-instance = **named mutex (CreateMutexW) at startup**; a second instance detects the existing one, **signals it to show the launcher window** (Win32 broadcast/named-event channel), then exits. No QSharedMemory, no process scan.
- **D-10:** Autostart toggle lives in the **settings window only** (no tray item). State read from the HKCU Run key when settings opens; toggle writes/removes the key.
- **D-11:** Autostart boots **quiet to tray, window hidden** (`--autostart` arg) — the resident-citizen feel; hotkey opens the launcher when wanted.
- **D-12:** Run key value = **quoted exe path + `--autostart`**: `"C:\...\wisp.exe" --autostart`. wisp parses the arg to skip the first show.
- **D-13:** Installer = **per-user NSIS, no admin/UAC**: installs to `%LOCALAPPDATA%\Programs\wisp`, writes a Start Menu shortcut. No machine-wide Program Files install.
- **D-14:** VC runtime = **download-and-install VC_redist.x64.exe if missing** (checked via registry presence); not bundled in the installer, no shipping of windeployqt-copied MSVC DLLs.
- **D-15:** LGPL verification = **compliance doc + relink test** in the repo (`packaging/LGPL-COMPLIANCE.md` or equivalent): THIRD-PARTY-NOTICES.txt ships in the installer; relink test = a test binary linking against the deployed Qt6Core.dll (proves dynamic linking); source-offer statement documented (Qt source URL + offer wording).
- **D-16:** Clean-VM validation = **manual runbook checklist** (not automated CI): documented steps for Win10 22H2 + Win11 24H2 VM snapshots — install → launch → hotkey → launch an app → search a file, plus the hotkey-conflict re-verification with another launcher owning Alt+Space.

### OpenCode's Discretion
Exact tray icon appearance (existing generated 16px disc stays or gets accent-aware retouch — planner's call), settings window layout details and row styling, named mutex name / signaling channel specifics (WM_COPYDATA vs named event), swatch color values (must be chosen for dark-theme readability), the custom QML color dialog's internal design, NSIS script structure and installer branding, relink-test build mechanics, VM runbook file location/format.

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope. Existing v2 exclusions reaffirmed: backdrop blur (VISU-04), recency ranking (LAUN-07/08), copy full path (LAUN-09), code signing (STACK: recommended, not required — not part of this phase).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SYS-01 | System tray icon with Open / Settings / Quit menu; single-instance enforcement | TrayIcon.cpp analog + CreateMutexW/named-event pattern (D-09); UI-SPEC locked menu order |
| SYS-02 | User can toggle "start with Windows" (autostart) in settings | QSettings NativeFormat = HKCU Run wrapper; quoted `"exe" --autostart` value (D-10/D-12) |
| SYS-03 | Settings window with hotkey capture, accent color picker, and autostart toggle | UI-SPEC geometry/copy/tokens locked; HotkeyCaptureDialog + SettingsStore analogs |
| SYS-04 | Installer works on clean Win10/11 (NSIS, windeployqt `--qmldir`, VC redist) with Qt LGPL notices | NSIS per-user verified (install-per-user.nsi pattern); dumpbin/relink test verified; D-13..D-16 |
| VISU-03 | User can pick an accent color used for selection and match highlighting | 9-swatch Theme.accentSwatches token + custom ColorDialog.qml; Phase-5 reactive wiring reused (D-05..D-08) |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| NSIS | 3.12 (winget NSIS.NSIS) | Per-user installer | Verified per-user pattern: `RequestExecutionLevel user` + `SetShellVarContext current` + `$LOCALAPPDATA\Programs` + HKCU uninstall keys — the canonical no-admin installer for Qt apps [CITED: nsis.sourceforge.io/Examples/install-per-user.nsi] |
| Qt (existing) | 6.11.1 | All app runtime | Unchanged — dynamic-link-only LGPL posture [VERIFIED: C:\Qt\6.11.1\msvc2022_64] |
| VC_redist.x64.exe | latest ~14.4x | MSVC runtime for clean VMs | Download at build time from `https://aka.ms/vs/17/release/vc_redist.x64.exe`; installer executes silently when registry check fails (D-14) [VERIFIED: STACK.md; registry check key confirmed on dev machine] |
| dumpbin (VS2022) | 14.44.35207 | Dynamic-link proof | `dumpbin /DEPENDENTS wisp.exe` shows Qt6*.dll imports — the LGPL "dynamically linked" evidence [VERIFIED: C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| winget | 1.29.280 | NSIS bootstrap | `winget install NSIS.NSIS --silent` in the installer-plan task — automatable, no user setup [VERIFIED: winget search NSIS → NSIS.NSIS 3.12] |
| Qt Test / CTest | bundled | Unit tests | Existing 17-test infra; new tst_singleinstance + tst_autostart follow the `qt_add_executable(tst_* tests/tst_*.cpp)` pattern [VERIFIED: CMakeLists.txt] |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Named event (SetEvent/WaitForSingleObject) | WM_COPYDATA broadcast | Event is simpler (no window handle needed), race-safe if the second instance *creates* the event before signaling; WM_COPYDATA needs a target hwnd. D-09 leaves it open — event chosen |
| QSettings NativeFormat for Run key | Win32 RegSetValueEx | QSettings IS the registry wrapper (CONTEXT canonical refs); same API as settings INI, testable with a scoped key |
| winget-installed NSIS | Manual install | Automatable → no user_setup item |
| Custom QML color dialog | QColorDialog native | Locked D-07; native dialog breaks dark-theme aesthetic |

## Architecture Patterns

### Pattern 1: `src/win/` firewall (existing)
**What:** Win32 detail behind a pure C++ interface; unit-testable without OS interaction.
**When to use:** CreateMutexW / named event → `src/win/WinSingleInstance.{h,cpp}`.
**Example:** WinHotkey/WinLaunch precedent (src/win/). `WinSingleInstance` exposes: `bool tryAcquire()` (CreateMutexW, ERROR_ALREADY_EXISTS → false), `void signalShow()` (create-or-open named event + SetEvent), and a QThread-based watcher emitting `showRequested()` (WaitForSingleObject loop). Same-process duplicate acquisition is testable in tst_singleinstance.

### Pattern 2: makeSettings factory (existing)
**What:** `QSettings` created via a factory so tests can inject a scoped key/scope.
**When to use:** AutostartManager reads/writes the Run key. `QSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat)`; `setValue("wisp", "\"<exe>\" --autostart")` / `remove("wisp")` + `sync()`. Test with an injected test-only registry path (LaunchHistory::makeSettings precedent).

### Pattern 3: Thin QML host controller (existing)
**What:** C++ class owning a QQuickWindow, loading qrc:/qt/qml/wisp/...qml, exposing Q_INVOKABLE API + signals; QML does layout/visuals only.
**When to use:** `src/ui/SettingsWindow.{h,cpp}` (HotkeyCaptureDialog analog) + `qml/SettingsWindow.qml`. Controller owns: show/center/fade-in (120ms), Esc/click-away dismissal with ~150ms grace + launcher exemption (checks active window is ours via QGuiApplication::focusWindow), reopen HotkeyCaptureDialog on hotkey-row click, setAccent passthrough.

### Pattern 4: Token-first QML (locked 01-UI-SPEC)
**When to use:** Every new color/spacing/duration in Theme.qml BEFORE use in SettingsWindow.qml / ColorDialog.qml. This phase also **pays the shipped-literal debt**: HotkeyCaptureDialog.qml's `#E5484D` → `Theme.danger`, radius 6 → `Theme.fieldRadius`, 36px wells → `Theme.fieldHeight` (UI-SPEC line 17, 57-58).

### Anti-Patterns to Avoid
- **Reaching into SettingsStore from TrayIcon:** TrayIcon takes `setAccent(QColor)` + repaints; main.cpp wires accentChanged → tray (keeps tst_tray linkable without store).
- **Polling the named event on the GUI thread:** watcher lives on a QThread; never WaitForSingleObject on the UI thread.
- **NSIS `admin` execution level:** breaks per-user install (D-13); the verified pattern uses `RequestExecutionLevel user` (NSIS docs: "installers that don't install into system folders nor write HKLM should specify user execution level") [CITED: NSIS Chapter 4].
- **Bundling windeployqt-copied MSVC DLLs:** forbidden (D-14, STACK.md) — VC_redist only.
- **Inline QML `component` blocks:** qmlcachegen rejects them (Phase-1 ruling) — write concrete rectangles.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Single-instance detection | Process scan / QSharedMemory | CreateMutexW (D-09 locked) | OS-atomic, standard; QSharedMemory leaves stale artifacts on crash |
| Run-key autostart | Raw RegSetValueEx + escaping | QSettings NativeFormat at the Run key path | Registry wrapper with quoting handled by value string; same API as existing stores |
| Installer | Custom copy script / MSI | NSIS 3.x per-user script | Verified example exists (install-per-user.nsi); MSI/WiX rejected in STACK.md |
| LGPL proof | Claiming compliance | dumpbin /DEPENDENTS + relink test binary | Mechanical evidence, both tools present on dev machine |
| VC runtime detection | Assuming installed | Registry check `HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64` → `Installed` DWORD=1 | Confirmed present on this machine (14.51.36247); NSIS `ReadRegDWORD` + `${If}` gates VC_redist exec |

**Key insight:** every hard problem in this phase was already solved in earlier phases or the research docs. The plans are mostly wiring + packaging with locked specs.

## Common Pitfalls

### Pitfall 1: Second-instance signal race
**What goes wrong:** Second instance signals before the first instance's watcher exists → lost "show" request.
**Why it happens:** Event channel not yet created when SetEvent is called.
**How to avoid:** Second instance uses CreateEventW (create-or-open semantics) before SetEvent — if the first instance hasn't started waiting, the event stays signaled and the watcher fires immediately. **Warning signs:** second launch shows nothing.

### Pitfall 2: NSIS default execution level breaks per-user install
**What goes wrong:** Installer prompts UAC and/or writes shortcuts to all-users locations.
**Why it happens:** Missing `RequestExecutionLevel user`; SetShellVarContext alone is not enough for modern Windows (verified forum pattern).
**How to avoid:** `RequestExecutionLevel user` + `SetShellVarContext current` in BOTH install and uninstall `.onInit`. **Warning signs:** UAC prompt during install.

### Pitfall 3: CMakeLists.txt conflicts across plans
**What goes wrong:** Parallel plans both editing the source list / QML_FILES list.
**Why it happens:** Every new source, QML file, and test target requires a CMakeLists edit.
**How to avoid:** Sequential waves (1 plan owns the file per wave) — the established Phase 4/5 convention. **Warning signs:** merge conflicts on CMakeLists.

### Pitfall 4: Dismissal grace killing the settings window on launcher show
**What goes wrong:** Hotkey opens launcher → settings window closes (click-away deactivation).
**Why it happens:** Deactivation-hide triggers on any focus loss (D-02.4 pattern).
**How to avoid:** Grace handler checks the newly-active window belongs to our process (QGuiApplication::focusWindow() comparison) and skips the close (UI-SPEC: "activating the launcher window must NOT close settings").

### Pitfall 5: Relink test silently passing against the dev Qt
**What goes wrong:** Test binary links/runs against C:\Qt libs, never touching the deployed DLLs.
**Why it happens:** PATH resolution picks up dev Qt.
**How to avoid:** Run the relink binary with PATH set to the deploy folder only (no C:\Qt in PATH), and have the binary print QLibraryInfo::location(QLibraryInfo::LibrariesPath) for evidence; dumpbin /DEPENDENTS on the deployed exe confirms import names.

## Code Examples

### NSIS per-user skeleton (verified pattern)
```nsis
; Source: nsis.sourceforge.io/Examples/install-per-user.nsi + NSIS Chapter 4
Name "wisp"
OutFile "wisp-setup.exe"
RequestExecutionLevel user          ; CRITICAL: no UAC, per-user install
InstallDir "$LocalAppData\Programs\wisp"

Function .onInit
  SetShellVarContext Current
FunctionEnd

Section "Install"
  SetOutPath "$InstDir"
  File /r "..\build\release\deploy\*"   ; windeployqt output
  WriteUninstaller "$InstDir\Uninst.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp" "DisplayName" "wisp"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\wisp" "UninstallString" '"$InstDir\Uninst.exe"'
  ; VC_redist if missing (D-14):
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  ${If} $0 != 1
    ExecWait '"$InstDir\vc_redist.x64.exe" /install /quiet /norestart'
  ${EndIf}
SectionEnd

Section "Start Menu shortcut"
  CreateShortcut /NoWorkingDir "$SMPrograms\wisp.lnk" "$InstDir\wisp.exe"
SectionEnd
```

### Single-instance + show signal (Win32)
```cpp
// Source: Win32 API (CreateMutexW/CreateEventW semantics — MS docs)
HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\wisp-single-instance");
if (GetLastError() == ERROR_ALREADY_EXISTS) {
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, L"Local\\wisp-show-launcher"); // create-or-open
  SetEvent(ev);   // stays signaled if first instance isn't waiting yet (Pitfall 1 fix)
  return false;   // second instance → exit
}
```

### Run-key autostart (QSettings)
```cpp
// Source: STACK.md (QSettings is a registry wrapper); value format locked by D-12
QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
              QSettings::NativeFormat);
run.setValue("wisp", "\"" + QCoreApplication::applicationFilePath() + "\" --autostart");
// disable: run.remove("wisp"); both followed by run.sync();
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| QHotkey library for hotkeys | Raw RegisterHotKey (Phase 2) | Phase 2 | Raw approach unchanged; settings reuses HotkeyManager |
| Native QColorDialog | Custom QML dialog | This phase (D-07) | Token-consistent dark aesthetic |
| WiX/MSI installers | Per-user NSIS | Project research (STACK.md) | Verified pattern for per-user Qt apps |

**Deprecated/outdated:**
- SHGetFileInfo icons, QtGraphicalEffects, static Qt linking — all rejected earlier, unchanged.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | winget install NSIS.NSIS works silently in the execution environment (admin not required for winget user-scope install) | Environment Availability | Low — winget v1.29 present; falls back to `--scope user` flag or manual install checkpoint |
| A2 | The `Local\` namespace mutex/event names are session-safe for single-user launcher | Single-Instance | Low — standard practice; RDP sessions get separate namespaces which is acceptable for a launcher |
| A3 | `aka.ms/vs/17/release/vc_redist.x64.exe` remains the current official redist URL | Installer | Low — stable Microsoft redirect for years; verify response is Microsoft-signed at build time |
| A4 | Click-away grace (150ms, launcher exemption) is implementable purely with focus-window checks in QML-adjacent C++ | Settings Window | Low-Medium — QGuiApplication::focusWindow + QWindow comparison; fallback: track activation via windowActivated in QML |

## Open Questions

1. **VC_redist bundling vs download-at-install-time**
   - What we know: D-14 says "download-and-install VC_redist.x64.exe if missing; not bundled in the installer".
   - What's unclear: whether "not bundled" means the installer downloads at install time (requires internet on the target VM) or the build script downloads it and the installer carries it.
   - Recommendation: **build-time download** — build-installer.ps1 downloads VC_redist.x64.exe into the deploy folder; NSIS only executes it when the registry check fails. Clean VMs may lack internet; a carried copy is the pragmatic read of "download-and-install". Executor implements build-time download (still respects "no windeployqt-copied MSVC DLLs").

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Qt 6.11.1 (MSVC 2022 x64) | All app code | ✓ | 6.11.1 | — |
| NSIS | Installer (SYS-04) | ✗ (installable) | 3.12 via winget | `winget install NSIS.NSIS --silent` in-plan; no user setup |
| VS2022 Community + dumpbin | LGPL verification | ✓ | MSVC 14.44.35207 | — |
| winget | NSIS bootstrap | ✓ | 1.29.280 | Manual NSIS install checkpoint |
| VC_redist.x64.exe | Clean-VM installs | ✗ (download at build time) | latest 14.4x | Registry check on dev machine already passes (14.51.36247) |
| Windows 10 22H2 / 11 24H2 VMs | SYS-04 manual runbook (D-16) | ✗ (user-provided) | — | Runbook documents the steps; execution = manual checkpoint |

**Missing dependencies with no fallback:**
- Clean Win10 22H2 / Win11 24H2 VM snapshots — D-16 is a manual runbook by design; becomes a human-verify checkpoint in the installer plan (user_setup).

**Missing dependencies with fallback:**
- NSIS → winget install in-plan (A1)
- VC_redist.x64.exe → build-time download from official aka.ms URL (A3)

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Qt Test (Qt6::Test) via CTest |
| Config file | CMakeLists.txt (BUILD_TESTING) |
| Quick run command | `cmake --build build/dev --target wisp && ctest --test-dir build/dev --output-on-failure` |
| Full suite command | `cmake --build build/dev && ctest --test-dir build/dev --output-on-failure` |
| Estimated runtime | ~20-40 seconds (17 existing + 2 new test targets) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| SYS-01 | Single-instance detection (mutex) + signal channel acquire semantics | unit | `ctest -R tst_singleinstance` | ❌ Wave 0 → plan 06-01 creates tests/tst_singleinstance.cpp |
| SYS-01 | Tray menu labels + settings signal + accent-aware repaint | unit | `ctest -R tst_tray` (updated) | ✅ existing, updated in 06-03 |
| SYS-02 | Autostart enable/disable/state via scoped registry key | unit | `ctest -R tst_autostart` | ❌ plan 06-01 creates tests/tst_autostart.cpp |
| SYS-03 | Settings window open/close semantics (controller API, QML load) | smoke (build + load) | `cmake --build build/dev && ctest -R tst_capture` (regression) | ✅ existing suite |
| SYS-03 | Hotkey re-registration via settings path | unit (existing) | `ctest -R tst_hotkey` | ✅ existing |
| VISU-03 | Swatch→setAccent persistence path | unit (existing) | `ctest -R tst_settings` | ✅ existing |
| SYS-04 | Dynamic linking proof + relink test | integration (script) | `packaging/verify-lgpl.ps1` (dumpbin + relink run) | ❌ plan 06-05 creates |
| SYS-04 | Installer builds | integration (script) | `packaging/build-installer.ps1` exit 0 + installer.exe exists | ❌ plan 06-05 creates |
| SYS-04 | Clean-VM install → launch → hotkey → app → search | manual-only | — (D-16 runbook) | runbook in 06-05 |

### Sampling Rate
- **Per task commit:** `cmake --build build/dev --target <target> && ctest -R <tst>` (targeted)
- **Per wave merge:** `cmake --build build/dev && ctest --test-dir build/dev --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/tst_singleinstance.cpp` — covers SYS-01 (mutex duplicate-acquisition semantics)
- [ ] `tests/tst_autostart.cpp` — covers SYS-02 (scoped Run-key registry writes via makeSettings-style injection)
- [ ] NSIS install — `winget install NSIS.NSIS --silent` (wave 5 task, not user setup)
- [ ] VC_redist download — build-time script step (wave 5 task)

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V1 Architecture | yes (minimal) | `src/win/` firewall pattern; no new trust boundaries |
| V2 Authentication | no | — (no accounts) |
| V3 Session Management | no | — |
| V4 Access Control | yes (local) | HKCU-only writes (Run key, settings INI); never HKLM |
| V5 Input Validation | partial | Hotkey sequence validation already locked (validateSequence); accent colors bounded to swatch list / dialog range |
| V6 Cryptography | no | — (no secrets; registry values are not secrets) |

### Known Threat Patterns for {stack}

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Run-key value injection (malformed path quoting) | Tampering | Quoted value `"<exe>" --autostart` (D-12); path comes from QCoreApplication::applicationFilePath(), not user input |
| Second-instance event spoofing (any session process SetEvent) | Spoofing | Session-local `Local\` namespace; worst case = launcher window pops — accepted nuisance |
| Installer tampering / fake VC_redist | Tampering | Build-time download from official aka.ms URL (A3); Microsoft-signed redist executable |
| QML surface injection | (none) | No web/remote content reaches QML; all data is local registry/INI values — no injection surface |
| Mutex name collision with another app | DoS | Distinctive `Local\wisp-*` names; failure mode is "second instance exits" — benign |

## Sources

### Primary (HIGH confidence)
- nsis.sourceforge.io/Examples/install-per-user.nsi — per-user NSIS pattern (RequestExecutionLevel user, SetShellVarContext current, FOLDERID_UserProgramFiles, HKCU uninstall keys)
- nsis.sourceforge.io/Reference/SetShellVarContext + Docs/Chapter4 — $LOCALAPPDATA / $SMPrograms semantics, execution-level guidance
- learn.microsoft.com CreateMutexW / CreateEventW / SetEvent / WaitForSingleObject — single-instance + event channel semantics (verified in STACK.md Phase-2 research; HIGH)
- Windows-classic-samples / STACK.md — HKCU Run key autostart (QSettings NativeFormat) (verified in project research)

### Secondary (MEDIUM confidence)
- NSIS Forums (2007, SetShellVarContext/UAC interplay) — confirms RequestExecutionLevel user is mandatory for current-user-only installs
- winget package listing (NSIS.NSIS 3.12) — live-verified availability

### Tertiary (LOW confidence)
- None — all flagged items are in the Assumptions Log instead

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all tools verified present/installable on this machine
- Architecture: HIGH — all patterns are existing-codebase analogs (TrayIcon, HotkeyCaptureDialog, makeSettings, src/win firewall)
- Pitfalls: HIGH — race/dismissal/CMake pitfalls derive from concrete codebase facts + verified NSIS docs

**Research date:** 2026-08-11
**Valid until:** 2026-09-10 (30 days; NSIS 3.12 and aka.ms URL are the only moving parts)
