# Phase 6 — Pattern Map

**Mapped:** 2026-08-11
**Source files:** 06-CONTEXT.md (D-01..D-16), 06-RESEARCH.md, 06-UI-SPEC.md (approved)
**Analog base:** existing codebase patterns from Phases 1–5

---

## Files to Create / Modify → Closest Analog

| Target File | Role / Data Flow | Closest Analog | Key Excerpts to Replicate |
|-------------|------------------|----------------|---------------------------|
| `src/win/WinSingleInstance.{h,cpp}` (NEW) | Win32 mutex + named-event channel behind firewall; pure interface consumed by main.cpp | `src/win/WinLaunch.cpp` (firewall pattern), `src/win/WinHotkey.cpp` | CreateMutexW + CreateEventW (create-or-open, Pitfall 1 fix); `bool tryAcquire()` → ERROR_ALREADY_EXISTS = second instance; `void signalShow()`; QThread watcher `showRequested()` signal |
| `src/core/AutostartManager.{h,cpp}` (NEW) | HKCU Run-key state; UI-thread-only store | `src/core/SettingsStore.{h,cpp}` (store shape, QSettings member, factory injection seam) | `SettingsStore.h`: Q_OBJECT, ctor `(const QString &settingsPath = {})`, empty → default key else injected seam; `makeSettings` factory: empty → `QSettings(IniFormat, UserScope, "TID", "wisp")` else `QSettings(path, IniFormat)` — autostart analog: `QSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat)` |
| `qml/Theme.qml` (MODIFY) | Token singleton extension (33 tokens + accentSwatches + danger) | existing `qml/Theme.qml` token table | `readonly property var accentSwatches: [...]` (9 values, UI-SPEC table); `danger: "#E5484D"`; toggle/field/swatch/geometry tokens per UI-SPEC Spacing Scale |
| `qml/SettingsWindow.qml` (NEW) | 480×360 tool window, 3 rows, token-only | `qml/MainWindow.qml` (window shell: Tool\|Frameless, transparent, shadow image, centerOnScreen, Connections) + `qml/HotkeyCaptureDialog.qml` (wells, keycap value, concrete widgets) | NO inline `component` (qmlcachegen); concrete rectangles; `Theme.*` everywhere |
| `qml/ColorDialog.qml` (NEW) | 280×320 ApplicationModal staged color dialog | `qml/HotkeyCaptureDialog.qml` (modal shell, 90×32 buttons, focus/Esc handling) | SV 160×160 two-stop gradient + overlay; hue bar 24×160; hex readout; OK/Cancel 90×32 |
| `src/ui/SettingsWindow.{h,cpp}` (NEW) | QML host controller (show/center/dismiss/launcher-exemption) | `src/ui/HotkeyCaptureDialog.{h,cpp}` (QML host, Q_INVOKABLE API, signals) | Hosts qrc:/qt/qml/wisp/SettingsWindow.qml; `open()`, `Q_INVOKABLE submitSequence`-style API; QGuiApplication::focusWindow() grace check |
| `src/tray/TrayIcon.{h,cpp}` (MODIFY) | Menu restructure + accent-aware icon | itself (existing TrayIcon) | Menu: addAction("Open wisp") → `addAction("Settings")` → `addAction("Change hotkey…")` → separator → addAction("Quit"); add `settingsRequested()` signal; `setAccent(QColor)` repaint via QPainter disc (line 22-23 pattern) |
| `src/app/main.cpp` (MODIFY) | Boot wiring: mutex first, `--autostart`, tray→settings, accent→tray | itself (existing boot order tray.show() → connect → hotkeys.start()) | Single-instance check BEFORE window creation; `QCoreApplication::arguments().contains("--autostart")`; SettingsWindow controller construction; `connect(store, &SettingsStore::accentChanged, tray, &TrayIcon::setAccent)` |
| `CMakeLists.txt` (MODIFY) | wisp_core sources + QML_FILES + new tst targets | itself (existing qt_add_library/executable/qml_module blocks) | `src/ui/HotkeyCaptureDialog.cpp` (line 35) in wisp_core → add WinSingleInstance.cpp + AutostartManager.cpp + SettingsWindow.cpp; QML_FILES (line 65-71) → add SettingsWindow.qml + ColorDialog.qml; tst pattern (lines 89-155): `qt_add_executable(tst_X tests/tst_X.cpp)` + `target_link_libraries(... Qt6::Core Qt6::Gui Qt6::Test wisp_core)` |
| `tests/tst_tray.cpp` (MODIFY) | Menu labels + signals assertions | itself | Update expected label list to [Open wisp, Settings, Change hotkey…, separator, Quit]; add settings signal check; accent setter repaint check |
| `tests/tst_singleinstance.cpp` (NEW) | Mutex semantics (same-process duplicate acquisition) | `tests/tst_capture.cpp` (static pure-method tests) | tryAcquire twice in one process → second returns false / reports exists |
| `tests/tst_autostart.cpp` (NEW) | Run-key write/remove/state via injected scoped key | `tests/tst_settings.cpp` (QTemporaryDir seam pattern) | AutostartManager(testKeyPath); setEnabled(true) → state true; setEnabled(false) → false |
| `packaging/installer.nsi` (NEW) | NSIS per-user installer | verified example: nsis.sourceforge.io/Examples/install-per-user.nsi | RequestExecutionLevel user; SetShellVarContext current (both .onInit); `$LocalAppData\Programs\wisp`; WriteUninstaller; HKCU uninstall keys; CreateShortcut "$SMPrograms\wisp.lnk"; VC_redist ReadRegDWORD gate |
| `packaging/build-installer.ps1` (NEW) | windeployqt output → VC_redist download → makensis | `deploy.ps1` (script discipline: $ErrorActionPreference, paths, echo) | Consumes deploy.ps1 output folder; `Invoke-WebRequest aka.ms/vs/17/release/vc_redist.x64.exe`; `& makensis installer.nsi` |
| `packaging/verify-lgpl.ps1` (NEW) | dumpbin /DEPENDENTS + relink test run | build.ps1 (script discipline) | dumpbin full path via VS2022; grep Qt6*.dll; run relink_test.exe with deploy-only PATH |
| `packaging/relink-test/main.cpp` (NEW) | LGPL dynamic-link proof binary | minimal Qt console app | QCoreApplication + QLibraryInfo::location(QLibraryInfo::LibrariesPath) print; exit 0 |
| `packaging/LGPL-COMPLIANCE.md` (NEW) | Compliance doc: NOTICES, relink evidence, source offer | — (doc; reference Qt LGPL obligations page) | Qt source URL, offer wording, test instructions |
| `packaging/VM-RUNBOOK.md` (NEW) | Clean-VM manual checklist (D-16) | — (doc) | Steps: install → launch → hotkey → app → search + conflict re-verify |
| `packaging/THIRD-PARTY-NOTICES.txt` (MODIFY) | LGPL notice content ships in installer | itself (existing stub) | Qt LGPLv3 notice + source offer paragraph |

---

## Cross-Cutting Constraints (from UI-SPEC / CONTEXT)

1. **Token-only rule:** zero literal colors/spacings in new QML; HotkeyCaptureDialog.qml debt (#E5484D, radius 6, 36px) tokenized to `Theme.danger` / `Theme.fieldRadius` / `Theme.fieldHeight` — grep-verifiable.
2. **No inline QML `component`** — concrete widgets only (qmlcachegen rejects inline components).
3. **Boot order:** single-instance check runs FIRST in main() (before window creation); `--autostart` suppresses first show; tray.show() → connects → hotkeys.start() unchanged.
4. **CMakeLists.txt single-owner per wave** — sequential wave ordering (Phase 4/5 convention).
5. **SettingsStore unchanged** — picker writes via existing `setAccent`; tray gets accent via setter (never reaches into the store).
6. **Dismissal grace exemption:** activating our own launcher window must NOT close settings.
