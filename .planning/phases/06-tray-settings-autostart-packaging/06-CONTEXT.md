# Phase 6: Tray, Settings, Autostart & Packaging - Context

**Gathered:** 2026-08-11
**Status:** Ready for planning

<domain>
## Phase Boundary

The app becomes a resident citizen — the release gate. Phase 6 delivers: (1) a tray icon
with Open / Settings / Quit menu and single-instance enforcement so a second launch
surfaces the existing instance (SYS-01); (2) a "start with Windows" autostart toggle
persisted to the quoted HKCU Run key (SYS-02); (3) a settings window with hotkey
capture, an accent color picker (VISU-03), and the autostart toggle (SYS-03); and (4) a
per-user NSIS installer that works on clean Win10 22H2 / Win11 24H2 VMs with LGPL
compliance verified (SYS-04).

**Success criteria (ROADMAP):** (1) tray with Open/Settings/Quit; second instance
surfaces the existing one; (2) autostart toggle works — after sign-out/sign-in the
launcher starts with tray present (quoted HKCU Run key, `--autostart` arg); (3) settings
window: capture new hotkey (re-registers and works immediately), pick accent (applies
live to selection + match highlighting), toggle autostart; (4) installer works on clean
Win10 22H2 and Win11 24H2 VMs: install → launch → hotkey → launch an app → search a
file; hotkey-conflict notification re-verified with another launcher owning Alt+Space;
(5) LGPL compliance verified: `THIRD-PARTY-NOTICES.txt` ships in the installer, Qt is
dynamically linked (relink test passes), source offer documented.

**Requirements:** SYS-01, SYS-02, SYS-03, SYS-04, VISU-03.

</domain>

<decisions>
## Implementation Decisions

### Settings Window (SYS-03)
- **D-01:** Settings = a **dedicated dark QML window** (new `SettingsWindow.qml`, built
  like MainWindow.qml: Theme tokens, concrete widgets, no QQC) backed by a small C++
  controller. NOT an extension of HotkeyCaptureDialog, NOT Qt Widgets.
- **D-02:** Window shape = **small themed tool window** (~480×360), non-modal,
  auto-centered on open, closes on Esc / click-away — same dismissal semantics as the
  launcher. No tabbed layout.
- **D-03:** Tray menu gains **Settings** ("Open wisp / Settings / Quit") AND **keeps
  "Change hotkey…"** — both paths stay: the tray item opens the existing fullscreen
  capture dialog directly; the settings window's hotkey row also reopens that same
  fullscreen `HotkeyCaptureDialog` when clicked (reuses the validated capture flow and
  `validateSequence`).
- **D-04:** The settings window opens from the tray (Settings item) only; the launcher
  window itself gains no settings affordance. Non-modal so the launcher can still pop
  over it.

### Accent Picker (VISU-03)
- **D-05:** Picker = **row of curated swatches (8–10 handpicked colors tuned for the
  dark theme) + a custom option**. No Windows-11 palette dump, no standalone wheel.
- **D-06:** Accent changes **apply live and persist on change** — the Phase-5 wiring
  already delivers this (SettingsStore::setAccent → accentChanged → Theme.accent);
  selection background, left bar, match-highlight chips all update instantly. No
  Save/Apply button.
- **D-07:** Custom option opens a **custom dark-themed QML color dialog** (not the
  native QColorDialog) — consistent with the app aesthetic; picks hue/saturation/value
  in the same token language.
- **D-08:** The 8–10 swatch values live in Theme.qml as tokens (token-only rule); the
  "custom" color is stored via the existing `setAccent`.

### Single-Instance & Autostart (SYS-01, SYS-02)
- **D-09:** Single-instance = **named mutex (CreateMutexW) at startup**; a second
  instance detects the existing one, **signals it to show the launcher window**
  (Win32 broadcast/named-event channel), then exits. No QSharedMemory, no process scan.
- **D-10:** Autostart toggle lives in the **settings window only** (no tray item).
  State read from the HKCU Run key when settings opens; toggle writes/removes the key.
- **D-11:** Autostart boots **quiet to tray, window hidden** (`--autostart` arg) — the
  resident-citizen feel; hotkey opens the launcher when wanted.
- **D-12:** Run key value = **quoted exe path + `--autostart`**:
  `"C:\...\wisp.exe" --autostart`. wisp parses the arg to skip the first show.

### Installer & LGPL Compliance (SYS-04)
- **D-13:** Installer = **per-user NSIS, no admin/UAC**: installs to
  `%LOCALAPPDATA%\Programs\wisp`, writes a Start Menu shortcut. No machine-wide
  Program Files install.
- **D-14:** VC runtime = **download-and-install VC_redist.x64.exe if missing**
  (checked via registry presence); not bundled in the installer, no shipping of
  windeployqt-copied MSVC DLLs.
- **D-15:** LGPL verification = **compliance doc + relink test** in the repo
  (`packaging/LGPL-COMPLIANCE.md` or equivalent): THIRD-PARTY-NOTICES.txt ships in the
  installer; relink test = a test binary linking against the deployed Qt6Core.dll
  (proves dynamic linking); source-offer statement documented (Qt source URL + offer
  wording).
- **D-16:** Clean-VM validation = **manual runbook checklist** (not automated CI):
  documented steps for Win10 22H2 + Win11 24H2 VM snapshots — install → launch →
  hotkey → launch an app → search a file, plus the hotkey-conflict re-verification
  with another launcher owning Alt+Space.

### OpenCode's Discretion
- Exact tray icon appearance (existing generated 16px disc stays or gets accent-aware
  retouch — planner's call), settings window layout details and row styling, named
  mutex name / signaling channel specifics (WM_COPYDATA vs named event), swatch color
  values (must be chosen for dark-theme readability), the custom QML color dialog's
  internal design, NSIS script structure and installer branding, relink-test build
  mechanics, VM runbook file location/format.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase Contract & Scope
- `.planning/ROADMAP.md` — Phase 6 goal, 5 success criteria (tray menu, single-instance
  surfacing, autostart quoted Run key + `--autostart`, live hotkey re-registration,
  clean-VM installer runs, LGPL: NOTICES + relink test + source offer).
- `.planning/REQUIREMENTS.md` — SYS-01..SYS-04, VISU-03 requirement text.

### Prior Phase Contracts (consume, don't re-discuss)
- `.planning/phases/02-global-hotkey-toggle/02-CONTEXT.md` — D-02.1 resident lifecycle,
  D-02.2 tray (QApplication + QSystemTrayIcon), D-02.5 hotkey INI persistence,
  D-02.6 capture dialog decisions; HOTK-02 conflict-notification path.
- `.planning/phases/05-theme-visual-polish/05-CONTEXT.md` — D-13..D-16 accent store
  contract (SettingsStore, `setAccent` persist-before-notify, live binding to
  Theme.accent), icon pipeline (reusable for a settings accent swatch preview).
- `.planning/phases/01-core-shell/01-UI-SPEC.md` — Approved shell design: 640×400,
  44px rows, 12px radius, Theme.qml token singleton (token-only rule — settings window
  must never introduce literal values).
- `.planning/STATE.md` — Locked decisions: Qt 6.11.1, settings INI at
  `%APPDATA%\TID\wisp\wisp.ini`, controller-owned policy, Windows firewall pattern
  (`src/win/` behind pure C++ interfaces).

### Research (locked stack & Windows APIs)
- `.planning/research/STACK.md` — Tray icon (QSystemTrayIcon, C++ — QML has no tray
  type), autostart (HKCU Run via QSettings NativeFormat; never HKLM), NSIS 3.x per-user
  + UAC plugin, windeployqt `--qmldir`, VC_redist.x64.exe (never ship copied MSVC
  DLLs), LGPL dynamic-linking carve-out.

### Code Contract References (existing groundwork — MUST read before planning)
- `src/tray/TrayIcon.{h,cpp}` — existing tray: Open wisp / Change hotkey… / Quit +
  `notifyHotkeyConflict`; gains a Settings action (menu order change).
- `src/core/SettingsStore.{h,cpp}` — accent store (D-14); Phase 6 adds hotkey +
  autostart surfaces or a sibling store — planner's call, follow the same makeSettings
  factory / QSettings discipline.
- `src/core/HotkeyManager.{h,cpp}` — INI-persisted hotkey, `setHotkey()` re-register
  (HOTK-01 configurable — the settings path reuses this).
- `src/ui/HotkeyCaptureDialog.{h,cpp}` + `qml/HotkeyCaptureDialog.qml` — existing
  validated capture dialog (F12/modifier-only rejected); reopened from settings.
- `src/app/main.cpp` — wiring host: tray/capture/hotkeys boot order (tray.show() →
  connect → hotkeys.start()), tray-less fallback; settings window + single-instance +
  `--autostart` parse wire in here.
- `deploy.ps1` — windeployqt `--qmldir` + qt.conf + THIRD-PARTY-NOTICES copy; the
  installer consumes its output folder.
- `packaging/THIRD-PARTY-NOTICES.txt` — existing notices stub that must ship in the
  installer.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/tray/TrayIcon.{h,cpp}` — complete tray implementation (generated 16px disc icon,
  QMenu owned explicitly, showMessage conflict notification). Phase 6 adds a Settings
  menu action + signal; menu order becomes Open / Settings / Change hotkey… / Quit.
- `src/ui/HotkeyCaptureDialog.{h,cpp}` + `qml/HotkeyCaptureDialog.qml` — fullscreen
  capture dialog with static `validateSequence` (unit-tested in tst_capture); settings
  reopens it via the same `open(currentSequence)` + `accepted` contract.
- `src/core/SettingsStore.{h,cpp}` — QSettings-over-INI store with NOTIFY (accent);
  the pattern for new settings surfaces (autostart flag, maybe swatch/custom accent).
- `src/core/HotkeyManager.{h,cpp}` — start()/setHotkey() lifecycle with conflict
  handling; the settings hotkey row just calls through.
- `src/core/LaunchHistory.cpp` — makeSettings() QSettings factory (guaranteed elision)
  + QMutex discipline precedent if any new store touches workers (none expected —
  Phase-6 stores are UI-thread-only like SettingsStore).
- `src/win/` firewall pattern — Win32 detail (CreateMutexW, signal channel, Run-key
  helpers if not QSettings-based) goes behind `src/win/WinSingleInstance.{h,cpp}` with
  a pure interface; unit-testable in tst_*.
- `qml/Theme.qml` — token singleton; swatch values + settings-window tokens extend
  here (token-only rule).
- `deploy.ps1` + `packaging/THIRD-PARTY-NOTICES.txt` — deploy output is the NSIS
  installer's payload; notices already copied by deploy.
- Tests: `tst_tray.cpp`, `tst_settings.cpp`, `tst_capture.cpp` — existing test seams
  for tray menu structure, accent store, capture validation.

### Established Patterns
- Controller-owned policy + thin QML; Windows detail behind `src/win/` firewall with
  pure C++ interfaces (WinHotkey/WinLaunch/WinFullscreenGuard precedent).
- Token-only QML (01-UI-SPEC): settings window and custom color dialog add Theme
  tokens first; no literals.
- QSettings NativeFormat INI at `%APPDATA%\TID\wisp\wisp.ini` for all user prefs
  (QSettings is a registry wrapper when pointed at HKCU Run — same API for autostart).
- Boot-order discipline in main.cpp: tray.show() → connects → hotkeys.start();
  single-instance check runs FIRST (before window creation) and the `--autostart` flag
  suppresses the first show.
- `wisp_core` static lib + `tests/tst_*` Qt-Test pattern (17 green) — WinSingleInstance
  and any settings-store extension get their own tst.

### Integration Points
- `src/app/main.cpp` — single-instance check at the very top of main(); SettingsWindow
  controller + QML registration; tray Settings action → settings controller open();
  `--autostart` parse → suppress initial window show (window hidden until hotkey/tray).
- `qml/` — new `SettingsWindow.qml` (+ custom color dialog component) registered as a
  second QML window; reuse `HotkeyCaptureDialog.qml` as-is.
- `src/core/SettingsStore` or sibling — autostart toggle state + accent swatch/custom
  persistence; tray Settings/Open/Quit wiring mirrors existing signals.
- `CMakeLists.txt` — wisp_core source list additions (WinSingleInstance, settings
  controller), qml module additions, NSIS packaging step target.

</code_context>

<specifics>
## Specific Ideas

- Success criterion 2 wording is exact: "quoted HKCU Run key, `--autostart` arg" — the
  Run key value is `"<path to wisp.exe>" --autostart` (D-12).
- Success criterion 3: "capture a new hotkey (re-registers and works immediately)" —
  the settings hotkey row must produce a working re-registration on close, not a
  staged value.
- Success criterion 5 uses the exact phrase "relink test passes" for dynamic-linking
  proof — the compliance doc must define and pass that test.
- The existing tray already satisfies part of SYS-01; the work is menu restructure
  (Settings action), single-instance, and the settings window itself.
- PowerToys Run remains the reference feel for launcher behavior; its tray+settings
  posture (resident, quiet at boot, settings off the tray) is the model.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope. Existing v2 exclusions reaffirmed:
backdrop blur (VISU-04), recency ranking (LAUN-07/08), copy full path (LAUN-09),
code signing (STACK: recommended, not required — not part of this phase).

</deferred>

---
*Phase: 6-tray-settings-autostart-packaging*
*Context gathered: 2026-08-11*
