---
phase: 06-tray-settings-autostart-packaging
reviewed: 2026-08-11T23:07:38Z
depth: standard
files_reviewed: 25
files_reviewed_list:
  - .gitignore
  - CMakeLists.txt
  - packaging/build-installer.ps1
  - packaging/installer.nsi
  - packaging/LGPL-COMPLIANCE.md
  - packaging/relink-test/main.cpp
  - packaging/THIRD-PARTY-NOTICES.txt
  - packaging/verify-lgpl.ps1
  - packaging/VM-RUNBOOK.md
  - qml/ColorDialog.qml
  - qml/HotkeyCaptureDialog.qml
  - qml/SettingsWindow.qml
  - qml/Theme.qml
  - src/app/main.cpp
  - src/core/AutostartManager.cpp
  - src/core/AutostartManager.h
  - src/tray/TrayIcon.cpp
  - src/tray/TrayIcon.h
  - src/ui/SettingsWindow.cpp
  - src/ui/SettingsWindow.h
  - src/win/WinSingleInstance.cpp
  - src/win/WinSingleInstance.h
  - tests/tst_autostart.cpp
  - tests/tst_singleinstance.cpp
  - tests/tst_tray.cpp
findings:
  critical: 2
  warning: 10
  info: 6
  total: 18
status: issues_found
---

# Phase 6: Code Review Report

**Reviewed:** 2026-08-11T23:07:38Z
**Depth:** standard
**Files Reviewed:** 25
**Status:** issues_found

## Summary

Phase 06 reviewed across five surfaces: Win32 single-instance channel (`WinSingleInstance`), HKCU Run-key autostart (`AutostartManager`), tray menu (`TrayIcon`), the QML settings surface (`SettingsWindow` C++/QML + `ColorDialog.qml`), and the packaging/LGPL pipeline (NSIS installer, `build-installer.ps1`, `verify-lgpl.ps1`). Cross-file call-chain tracing included `HotkeyCaptureDialog.cpp`, `HotkeyManager::setHotkey`, `MainWindow.qml`, and `deploy.ps1` to verify wiring claims.

The Win32 plumbing (`WinSingleInstance` mutex/event lifecycle, thread shutdown, `AutostartManager` registry seam, `TrayIcon` ownership) is sound: handle closes are ordered after thread join, the create-or-open Pitfall-1 fix is genuinely handled, and the QSettings Run-key path is the canonical Qt-documented pattern. The packaging pipeline is functional and the LGPL evidence chain is mostly honest.

However, **two core Phase-06 features are functionally dead**:

1. **The settings window's hotkey row does nothing.** `SettingsWindow.qml` emits `hotkeyRowClicked` (mouse *and* keyboard paths) but nothing — neither the C++ controller nor the QML itself — is connected to it. `openHotkeyCapture()` exists on the controller and is never called.
2. **The capture dialog can be opened exactly once per session.** `HotkeyCaptureDialog::open()` early-returns on a `QPointer` that never nulls (the QML Window is engine-owned, hide-only on accept/cancel). Every subsequent tray "Change hotkey…" click is a silent no-op until restart.

Both contradict the phase's own success criteria and the VM-RUNBOOK's claim that step 7 passed. The capture dialog's invalid-sequence red-label path (`invokeMethod("showValidationError")`) also cannot work — the function is declared on a child `Text`, not the root window. Several installer/LGPL-evidence issues (VC_redist elevation assumption + unchecked `ExecWait`, no checksum on the downloaded redist, an evidence list missing Qt6Widgets.dll, and a relink "evidence" line that prints the dev-Qt path) weaken the release pipeline and should be fixed before shipping.

## Critical Issues

### CR-01: Settings hotkey row is dead — `hotkeyRowClicked` never wired to anything

**Files:** `qml/SettingsWindow.qml:48,156-158,180`, `src/ui/SettingsWindow.cpp:130`, `src/ui/SettingsWindow.h:57`

**Issue:** The hotkey row's mouse click (`MouseArea` onClicked → `root.hotkeyRowClicked()`) and keyboard activation (Return/Enter/Space on `hotkeyWell`) emit the QML signal `hotkeyRowClicked` — but no handler exists. `SettingsWindow.cpp` never connects to it (grep across `src/` finds only the declaration and the Q_INVOKABLE `openHotkeyCapture()`), and `SettingsWindow.qml` has no `onHotkeyRowClicked:` handler. The controller's `openHotkeyCapture()` is invoked by nothing. The D-03 "hotkey-row handoff" contract — the flagship interaction of this phase's settings window — is completely non-functional: clicking "Hotkey / Click to change" does nothing. The only working entry point is the tray menu, which is itself single-use (see CR-02).

**Fix:** Wire the signal to the existing invokable — either connect in C++ after the window is created:
```cpp
// in SettingsWindow::ensureWindow(), after m_window is set:
if (auto *content = m_window->contentItem()) {
    // or use the window root object:
}
// Simplest: connect on the root object
QObject::connect(m_window, SIGNAL(hotkeyRowClicked()),
                 this, SLOT(openHotkeyCapture()));
```
or drop the signal indirection in the QML and call the controller directly:
```qml
onClicked: { if (settingsController) settingsController.openHotkeyCapture() }
```
(and likewise for the three `Keys.on*Pressed` handlers).

### CR-02: Hotkey capture dialog is single-use — every open after the first is a silent no-op

**File:** `src/ui/HotkeyCaptureDialog.cpp:47-48,66,80-81` (+ `qml/HotkeyCaptureDialog.qml:196-201`)

**Issue:** `open()` begins with `if (m_dialog) return; // already open — one capture at a time`. `m_dialog` is a `QPointer<QQuickWindow>` whose target is **never destroyed**: the window is created engine-owned via `beginCreate`, and on accept it is only `hide()`n (`m_dialog->hide()`); on cancel the QML calls `root.hide(); root.close()` which also just hides. Nothing deletes the QQuickWindow, so the QPointer never nulls and every subsequent `open()` call — from the tray "Change hotkey…" menu — returns immediately. After the first successful hotkey change in an app session, the feature is dead until restart. The `main.cpp` comment claiming "hotkey-capture handoff reopens the EXISTING dialog" describes the intent, not the behavior.

**Fix:** Reuse the hidden window instead of gating on pointer existence:
```cpp
void HotkeyCaptureDialog::open(const QString &currentSequence)
{
    if (m_dialog) {
        if (m_dialog->isVisible())
            return;                      // genuinely open — one capture at a time
        m_dialog->setProperty("currentSequence", currentSequence);
        m_dialog->show();
        m_dialog->requestActivate();
        return;
    }
    // ... existing create path ...
}
```
Alternatively, destroy on close (`QQmlEngine::setObjectOwnership(dialogObj, QQmlEngine::JavaScriptOwnership)` + `deleteLater` in `cancelDialog`/accept) so the `QPointer` nulls naturally.

## Warnings

### WR-01: ColorDialog seeds hue with -1 for achromatic accents — commits a wrong color

**File:** `qml/ColorDialog.qml:83-85,36-52`

**Issue:** `stageHue = Theme.accent.hsvHue` — `QColor::hsvHue()` returns **-1** for achromatic colors (saturation 0). A user can reach that state: the SV square's left edge sets `stageSat = 0`, and committing produces a grey accent. On the next open, `stageHue = -1`; `hsvToHex(-1, s, v)` computes `i = floor(-6) = -6`, `i % 6 = 0` → `r=v, g=v, b=v*(1-s)` — a red-family color — and the SV-square gradient (`Qt.hsla(-1, ...)`) also wraps to red. Pressing OK then commits a color the user never picked.

**Fix:** Clamp the seed: `stageHue = Math.max(0, Theme.accent.hsvHue)` (and `stageSat`/`stageVal` are already safe). Also guard `updateHue`/`updateSatVal` inputs if desired (they are already clamped).

### WR-02: `invokeMethod("showValidationError")` cannot find the function — invalid combos get no red label

**Files:** `src/ui/HotkeyCaptureDialog.cpp:73-76`, `qml/HotkeyCaptureDialog.qml:133-138`

**Issue:** `QMetaObject::invokeMethod(m_dialog, "showValidationError", ...)` looks the method up on the **root Window object** — but `showValidationError()` is declared on the child `Text` (`id: hint`). QML JS functions are attached to the object that declares them; the root has no such method, so the invoke fails silently. Result: pressing OK with an F12 or modifier-only sequence leaves the dialog open with **no feedback at all** — the HOTK-02 "red rejection label" path is dead code. (VM-RUNBOOK step 7's claim of "red conflict labels" is not reproducible from this code for the invalid-sequence path.)

**Fix:** Move the function to the root Window (it can reference the hint `Text` by id), e.g.:
```qml
// on root Window:
function showValidationError() {
    hint.color = Theme.danger
    hint.text = capturedSequence.indexOf("F12") >= 0
        ? "F12 is reserved by Windows and cannot be used"
        : "Modifier keys alone are not a valid combination"
}
```
(the `hint` id resolves from the root scope). Optionally also reset the label on the next key press.

### WR-03: `launcherController` context property set AFTER `loadFromModule`

**File:** `src/app/main.cpp:161-164`

**Issue:** `engine.loadFromModule("wisp", "MainWindow")` instantiates the window (line 161), and only then `setContextProperty("launcherController", ...)` (line 164). This works **only** because every use in `MainWindow.qml` is a call-time `stateNote()` inside key/click handlers (verified). Declarative bindings that referenced `launcherController` would evaluate during instantiation, resolve to `undefined`, and — since context properties have no change notification — stay `undefined` forever. The comment block at lines 73-76 explicitly states the load-bearing rule ("Constructed BEFORE loadFromModule so the context properties exist when MainWindow.qml type-compiles"), and this property silently violates it. A future refactor (e.g., `text: launcherController.someProperty`) breaks with no error.

**Fix:** Move `engine.rootContext()->setContextProperty("launcherController", &controller);` above `engine.loadFromModule(...)`.

### WR-04: Uninstall leaves the HKCU Run value behind

**File:** `packaging/installer.nsi:67-71`

**Issue:** The uninstall section removes the shortcut, the Uninstall key, and the install dir — but never `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\wisp`, which `AutostartManager::setEnabled(true)` may have written. After uninstall, a stale Run entry points at a deleted exe; worse, a **reinstall** reads the leftover value via `AutostartManager::isEnabled()` and shows "Start with Windows" ON even though the user never enabled it in the new install.

**Fix:** Add to the uninstall section:
```nsis
DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "wisp"
```
(and consider removing `%APPDATA%\TID\wisp` settings — optional, but the Run value is required).

### WR-05: VC_redist step assumes silent, unelevated install; failure unchecked

**File:** `packaging/installer.nsi:53-58`

**Issue:** Two problems with the redist gate:
1. `vc_redist.x64.exe /install /quiet` **requires elevation** (it writes HKLM/Program Files). Run from the unelevated per-user installer, it will pop a UAC consent prompt on a clean, UAC-enabled machine — contradicting `VM-RUNBOOK.md` step 2's expectation of "no UAC prompt" / "VC_redist installs silently (no visible prompt)". Either the VMs had UAC disabled (a deviation the runbook does not record) or the claim is wrong.
2. `ExecWait`'s return code is never checked. If the user cancels the consent prompt (or the redist fails), wisp is installed without the MSVC runtime and fails to start with a cryptic loader error — no abort, no warning.

**Fix:** Check the result and surface it:
```nsis
ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
${If} $0 != 1
  ExecWait '"$InstDir\vc_redist.x64.exe" /install /quiet /norestart' $1
  ${If} $1 != 0
    MessageBox MB_ICONEXCLAMATION|MB_OK "The Microsoft VC++ runtime could not be installed (error $1). wisp requires it to run.$\r$\nYou can install it manually from https://aka.ms/vs/17/release/vc_redist.x64.exe"
  ${EndIf}
${EndIf}
```
and update the runbook to expect the consent prompt for the redist on clean machines.

### WR-06: VC_redist download has no integrity check before the installer executes it

**File:** `packaging/build-installer.ps1:35-41`

**Issue:** The redist is downloaded over TLS from aka.ms with no checksum verification, then embedded in the installer and executed (elevated!) on target machines. A TLS-intercepting proxy / poisoned build machine could substitute an arbitrary executable that ships inside `wisp-setup.exe` and runs with consent elevation on every clean install. T-06-03's "official URL only" gate is not an integrity guarantee.

**Fix:** Pin and verify a SHA-256 (or Authenticode signature check):
```powershell
$expectedHash = "..." # pin the current official vc_redist.x64.exe hash at release time
$actual = (Get-FileHash $redist -Algorithm SHA256).Hash
if ($actual -ne $expectedHash) { throw "vc_redist.x64.exe hash mismatch - refusing to package" }
```
(at minimum verify the Authenticode signature: `Get-AuthenticodeSignature $redist` → `Status -eq Valid` and subject CN=Microsoft).

### WR-07: LGPL Check 1 asserts only 4 Qt DLLs — Qt6Widgets.dll and Qt6Concurrent.dll missing

**File:** `packaging/verify-lgpl.ps1:30-33`

**Issue:** The dumpbin gate checks `Qt6Core/Gui/Qml/Quick` only. The app also links `Qt6::Widgets` (QApplication, QSystemTrayIcon — `main.cpp:51`, `CMakeLists.txt:86`) and `Qt6::Concurrent` (`CMakeLists.txt:41`). The gate would pass even if those two were statically embedded — the evidence does not cover the full link set, and `THIRD-PARTY-NOTICES.txt`/`LGPL-COMPLIANCE.md` claim the evidence is complete.

**Fix:** Extend the assertion list:
```powershell
foreach ($dll in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll", "Qt6Widgets.dll", "Qt6Concurrent.dll")) {
```

### WR-08: Relink test prints the DEV Qt path as its "evidence"

**Files:** `packaging/relink-test/main.cpp:19-20`, `packaging/LGPL-COMPLIANCE.md:53`

**Issue:** `QLibraryInfo::path(QLibraryInfo::LibrariesPath)` returns the **compile-time** prefix (no `qt.conf` next to `relink-test.exe`), i.e. `C:/Qt/6.11.1/msvc2022_64/lib`. The runtime-loaded `Qt6Core.dll` genuinely comes from the deploy folder (exe dir / System32 / PATH=deploy-only search order — the proof holds), but the printed line — which `LGPL-COMPLIANCE.md` explicitly calls "the resolved Qt libraries path as evidence" — shows the *dev* path. Anyone reading the log would conclude the opposite of what the check claims to prove.

**Fix:** Print the actually-loaded module path:
```cpp
#include <windows.h>
HMODULE h = GetModuleHandleW(L"Qt6Core.dll");
wchar_t buf[MAX_PATH];
DWORD n = GetModuleFileNameW(h, buf, MAX_PATH);
std::printf("RELINK OK - loaded Qt6Core.dll from: %S\n", buf);
```

### WR-09: Installer staleness check misses non-code assets and CMakeLists.txt

**File:** `packaging/build-installer.ps1:17-18`

**Issue:** The rebuild gate watches only `.cpp/.h/.hpp/.qml/.qrc/.rc/.ui`. `qml/assets/shadow.png` is embedded in the binary via the QML module `RESOURCES` (`CMakeLists.txt:78-80`) — a change to it (e.g., regenerated by `scripts/generate-shadow.ps1`) will not mark the release binary stale, so the installer packages an old binary with the old shadow. Likewise a `CMakeLists.txt` change (new source file) ships a stale exe.

**Fix:** Add `".png"` (or the full `qml/assets/*` glob) and `CMakeLists.txt` to the watched extensions/paths:
```powershell
$newer = Get-ChildItem "src", "qml" -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in @(".cpp", ".h", ".hpp", ".qml", ".qrc", ".rc", ".ui", ".png") -and $_.LastWriteTime -gt $exeTime }
$newer += Get-Item "CMakeLists.txt" -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $exeTime }
```

### WR-10: THIRD-PARTY-NOTICES module list doesn't match the build configuration it cites

**File:** `packaging/THIRD-PARTY-NOTICES.txt:18-26`

**Issue:** Section 2 lists "Qt Core, Qt Gui, Qt Quick, Qt QML" and claims "These match the Qt components linked by the application's build configuration — see CMakeLists.txt find_package/Qt components." `CMakeLists.txt:7` links `Quick Qml Gui Core Test Widgets Concurrent`; the shipped app uses **Qt Widgets** (QApplication, QSystemTrayIcon) and **Qt Concurrent** (`wisp_core`). The notice understates the shipped modules — for a compliance artifact, an inaccurate claim is worse than none.

**Fix:** Add `Qt Widgets` and `Qt Concurrent` to the list (excluding `Test`, which is dev-only), or reword to "Qt Core, Gui, Quick, QML, Widgets, Concurrent".

## Info

### IN-01: Duplicate `capture.accepted → setHotkey` wiring

**Files:** `src/app/main.cpp:261-264`, `src/ui/SettingsWindow.cpp:43-47`

**Issue:** Both `main.cpp` and the `SettingsWindow` constructor connect `HotkeyCaptureDialog::accepted` to `hotkeys.setHotkey(...)`. Both fire per accept; this is benign **only** because of the equality guard at `HotkeyManager.cpp:135-136` (`if (seq == m_hotkey) return;`). If that guard ever changes, the hotkey is registered/persisted twice and `hotkeyChanged`/`registrationFailed` can double-fire. Keep one owner.

**Fix:** Remove the connection in `main.cpp` (the `SettingsWindow` connection also emits `currentHotkeyChanged`, which the settings well needs).

### IN-02: `--autostart` parsed but never consumed

**File:** `src/app/main.cpp:69-71`

**Issue:** `autostartBoot` is computed and immediately `Q_UNUSED`. The D-12 contract is satisfied only as a parse; no code path branches on it. If the intended semantics are "both boot paths stay hidden", the parse is pure ceremony — drop it or actually gate the first-show behavior.

### IN-03: `currentSequence` prefill is set but never displayed

**File:** `src/ui/HotkeyCaptureDialog.cpp:63`, `qml/HotkeyCaptureDialog.qml:108`

**Issue:** `open()` sets `currentSequence` on the dialog, but the capture well's text is `capturedSequence ? capturedSequence : "Press keys…"` — `currentSequence` is never read. The user is never shown what the current hotkey is.

### IN-04: verify-lgpl.ps1 hardcodes VS2022 Community and C:\Qt paths

**File:** `packaging/verify-lgpl.ps1:11,21,44`

**Issue:** `C:\Program Files\Microsoft Visual Studio\2022\Community\...` and `C:\Qt\6.11.1\msvc2022_64` are baked in. VS Build Tools/Enterprise or another Qt install aborts the script with a confusing error. Consider `vswhere` for the toolchain and `$env:QTDIR` for Qt.

### IN-05: `startWatching()` called twice leaks the first event handle

**File:** `src/win/WinSingleInstance.cpp:79`

**Issue:** A second `startWatching()` after `stopWatching()` overwrites `m_event` with a fresh `CreateEventW` handle — the previous handle is never closed. Currently unreachable (single call in `main.cpp`), but it is a latent leak if the class is ever reused.

### IN-06: Tray-less fallback starts hotkeys without a conflict notification channel

**File:** `src/app/main.cpp:290`

**Issue:** In the tray-less branch, `hotkeys.start()` may emit `registrationFailed` with no receiver — HOTK-02's "never silent" contract holds only when the tray exists. On exotic systems the conflict is silent. A `qWarning` (at least) is warranted.

---

_Reviewed: 2026-08-11T23:07:38Z_
_Reviewer: OpenCode (gsd-code-reviewer)_
_Depth: standard_
