# Stack Research

**Domain:** Windows application launcher (Rofi-style, Qt6 + QML)
**Researched:** 2026-08-09
**Confidence:** HIGH (all critical APIs verified against official Qt/MS docs on this date)

## Executive Position

This is a **Qt UI + raw Win32/COM backend** project. Nothing in the launcher domain (hotkeys, app enumeration, search index, icons, elevation, backdrop blur) has a first-class Qt API — every one of these goes through Win32/COM, with Qt providing the event loop bridge and the rendering surface. The stack below is deliberately thin: Qt 6.11 + MSVC 2022 + CMake, plus ~6 well-established Win32/COM entry points. No fuzzy-search library, no app-index database, no windowing abstraction layer — those are all custom code in this domain (PowerToys Run, Albert, and Rofi all ship in-house matchers).

---

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|---|---|---|---|
| Qt (open-source, LGPLv3) | **6.11.1** (current stable; support until 2027-03-17) | UI framework: Qt Quick/QML UI, Qt Core, Qt Gui, Qt Widgets (tray icon only) | QML animations hold 60fps with trivial code (already decided); 6.11 is the newest open-source-available line with longest support runway. **Do NOT use 6.8 LTS**: its patch releases (6.8.4+) are commercial-only, leaving open-source users stuck at 6.8.3 with no security fixes. |
| Toolchain | MSVC 2022 (x64, v143), C++20 | Compiler | The official Qt Windows binaries are MSVC-builds; windeployqt expects them. MinGW works but is second-class (no debugger integration in some tools, slower CI images). |
| Build system | CMake ≥ 3.25 + Ninja | Build orchestration | Qt 6 is CMake-native (`find_package(Qt6 COMPONENTS Quick Core Gui Widgets)`). Ninja is the standard fast generator; use the CMake/Ninja bundled with the Qt online installer to guarantee version compatibility. |
| C++/WinRT | Microsoft.Windows.CppWinRT NuGet (latest 2.x) | UWP/Store app enumeration & activation via `Windows.Management.Deployment.PackageManager` | This is the documented, header-only way to call WinRT APIs from C++; the COM-based alternative (roapi/IActivationFactory manual marshalling) is error-prone boilerplate. C++/WinRT ships in the Windows SDK; the NuGet pins a current copy. |
| Windows SDK | 10.0.22621+ (latest installed) | Win32/COM headers (dwmapi, shobjidl_core, searchapi, winuser) | Required for every Windows integration API below. |

### Windows Integration API Map (the actual domain work)

| Concern | API / Technique | Confidence | Why |
|---|---|---|---|
| Global hotkey | `RegisterHotKey(NULL, id, MOD_ALT\|MOD_NOREPEAT, VK_SPACE)` + `QAbstractNativeEventFilter::nativeEventFilter()` watching `eventType == "windows_dispatcher_MSG"` / `MSG.message == WM_HOTKEY` | HIGH (Qt doc + MS doc verified 2026-08-09) | The Qt doc explicitly names this mechanism ("system-wide messages such as messages from a registered hot key"). `RegisterHotKey` with `hWnd=NULL` posts WM_HOTKEY to the thread queue, which Qt's dispatcher delivers to the filter. Use `MOD_NOREPEAT` (Vista+) to avoid toggling spam. **Handle registration failure** (`RegisterHotKey` returns 0 when the combo is taken) — surface "hotkey in use" to the user, don't silently fall back. |
| Classic app enumeration | `SHGetKnownFolderPath(FOLDERID_Programs / FOLDERID_CommonPrograms)` → recurse `*.lnk` → parse with COM `IShellLinkW` + `IPersistFile::Load` (CoCreateInstance `CLSID_ShellLink`) | HIGH (well-established; MS docs stable) | Start Menu shortcuts are the canonical app inventory on Windows (PowerToys Run does exactly this). Don't skip `FOLDERID_CommonPrograms` (all-users) or nested folders. Read `GetPath`, `GetDescription`, `GetIconLocation` from each .lnk. |
| UWP/Store app enumeration | `Windows.Management.Deployment.PackageManager.FindPackagesForUser(L"")` (empty SID = current user) → iterate `Package.Applications()` → `AppListEntry.DisplayInfo` for name/logo | HIGH (MS WinRT docs + PowerToys Run docs verified) | This is precisely PowerToys Run's Program plugin strategy ("the PackageManager retrieves all the packages for the current user and indexes all the applications"). Launch later via `IApplicationActivationManager::ActivateApplication(aumid)` where `aumid = PackageFamilyName + "!" + appId-from-manifest`. |
| File search | COM: `ISearchManager` → `GetCatalog(L"SystemIndex")` → `ISearchCatalogManager::GetQueryHelper()` → `ISearchQueryHelper::GenerateSQLFromUserQuery()` (AQS) + `get_ConnectionString()` → run resulting SQL via OLE DB | HIGH (MS docs verified) | This is the documented way to query the Windows Search index. `GenerateSQLFromUserQuery` handles AQS escaping for you. Run the query on a worker thread (OLE DB calls block). Requires the Search indexer to be running (it is by default on Win10/11 for user libraries). Reference: Microsoft's own DSearch sample (Windows-classic-samples). |
| App icons | `SHCreateItemFromParsingName(path)` → `IShellItemImageFactory::GetImage(size, SIIGBF_ICONONLY \| SIIGBF_RESIZETOFIT)`; UWP icons via `AppListEntry.DisplayInfo.GetLogo()` | HIGH (MS docs verified) | `GetImage` returns an HBITMAP at any requested size (crisp at 32px with per-monitor DPI). MS explicitly warns: **never call it on the UI thread without `SIIGBF_INCACHEONLY`** — extract icons on a worker thread and cache results. Convert HBITMAP → QImage → QPixmap; hand to QML via a QQuickImageProvider or a model role. |
| Acrylic / blur backdrop | Win11 22H2+: `DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE /*38*/, DWMSBT_TRANSIENTWINDOW /*3 = acrylic*/)` — window must be transparent/`Qt::WA_TranslucentBackground`. Win10 fallback: `SetWindowCompositionAttribute` (ACCENT_ENABLE_BLURBEHIND=3). Final fallback: translucent window, no blur. | HIGH (MS docs verified) | `DWMSBT_TRANSIENTWINDOW` is the documented "Desktop Acrylic" — exactly the launcher look (like PowerToys Run / Spotlight). **Windows 10 has no supported acrylic API** — MS now documents `SetWindowCompositionAttribute` but remarks "not recommended; use DwmSetWindowAttribute instead" (which is Win11-only). So: feature-detect via `RtlGetVersion` / DwmSetWindowAttribute return value; on Win10 use the undocumented blur (works, minor drag-lag tradeoff) or a solid dark background — decide by feel during the UI phase. |
| Elevated launch | `ShellExecuteEx` with `lpVerb = L"runas"`, `SEE_MASK_NOCLOSEPROCESS`, wait on the returned process handle | HIGH (well-established) | `QProcess` **cannot** elevate — it has no verb concept. For a .lnk target, resolve the target path first (IShellLink), then `ShellExecuteEx` with `runas` + `lpParameters`. Detect user-cancelled UAC (process handle immediately closed / `SE_ERR_ACCESSDENIED`) and don't error-spam. |
| Tray icon | `QSystemTrayIcon` (C++ — it lives in Qt Widgets, not QML; expose state to QML via a context property or handle menus in C++) | HIGH (Qt docs) | Standard, stable since Qt 4. QML has no tray type; keep a tiny Widgets-linked C++ class for tray + menu. |
| Autostart | `QSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat)` → set value = quoted exe path; remove on disable | HIGH (well-established) | HKCU Run is the standard, no-admin, per-user autostart mechanism (what most consumer apps do). Never write HKLM (needs admin). QSettings is literally a registry wrapper — no extra dependency. |

### Supporting Libraries

| Library | Version | Purpose | When to Use |
|---|---|---|---|
| None for search matching — write it | — | Fuzzy scoring (fzf-style subsequence scoring with word-boundary bonuses) | Always. There is no de-facto Qt fuzzy-search library; PowerToys Run, Albert, and Rofi all ship in-house matchers. A scoring function is ~150 lines of C++; skip it only if a spike proves otherwise. |
| QHotkey (Skycoder42) | latest | RegisterHotKey wrapper with QKeySequence parsing | Optional convenience only. It's a thin wrapper over the same RegisterHotKey mechanism. If "configurable hotkey UI" arrives late, raw RegisterHotKey + a small keycode mapper is just as easy and removes a dependency. Start raw; add QHotkey only if key-sequence UX turns out fiddly. |
| Qt SQL (SQLite driver) | bundled with Qt | Persisted app index | Not for v1. Enumerating ~500 .lnk + ~100 UWP packages takes well under 100ms on a worker thread — no DB needed. Revisit only if the index grows (e.g., caching search results) or startup measurements demand it. |
| Microsoft Visual C++ Redistributable | VC_redist.x64.exe (latest, ~14.4x) | MSVC runtime for end users | Install alongside the app. windeployqt may copy compiler DLLs as a fallback, but the Qt docs explicitly state those "are not intended or licensed for redistribution" — ship the official redist installer instead. |

### Development Tools

| Tool | Purpose | Notes |
|---|---|---|
| Qt online installer (open source) | Qt 6.11.1 + bundled CMake/Ninja | Select: Qt Quick, Qt Core, Qt Gui, Qt Widgets, Qt SQL, Qt 5 Compatibility Module (only if QtGraphicalEffects needed — prefer MultiEffect, skip it), Developer and Designer Tools |
| Qt Creator 20.x (optional) | IDE | Fine for QML editing; not required — CMake + any editor works |
| NSIS 3.x (+ UAC plugin) | Installer | See packaging section |
| windeployqt | Deployment | Ships with Qt; run with `--qmldir` so QML imports get scanned |
| vswhere / Visual Studio Installer | Toolchain mgmt | Install "Desktop development with C++" workload |

---

## Windows Integration: What Each Piece Looks Like

### 1. Global hotkey (verified pattern)
```cpp
// HotkeyFilter.h
class HotkeyFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override {
        if (eventType == "windows_dispatcher_MSG") {           // system-wide messages
            MSG *msg = static_cast<MSG *>(message);
            if (msg->message == WM_HOTKEY && msg->wParam == hotkeyId) {
                emit trigger();  // or queue into the event loop
                return true;
            }
        }
        return false;
    }
};
// Registration: RegisterHotKey(nullptr, hotkeyId, MOD_ALT | MOD_NOREPEAT, VK_SPACE);
// Install: qApp->installNativeEventFilter(&filter);
```
Qt doc verified: on Windows the filter receives `"windows_generic_MSG"` (toplevel windows) and `"windows_dispatcher_MSG"` (system-wide, i.e. hotkeys). This is the canonical Qt6 approach — no Qt-based alternative exists, and none is needed.

### 2. Search query (verified pattern)
```
ISearchManager → ISearchCatalogManager (GetCatalog(L"SystemIndex"))
             → ISearchQueryHelper (GetQueryHelper)
             → GenerateSQLFromUserQuery(L"my query")   // AQS → SQL
             → get_ConnectionString()                  // OLE DB provider string
             → execute via OLE DB (or ADO) on worker thread
```
Bind `System.Search.Rank` / `System.ItemNameDisplay` / `System.ItemPathDisplay`, cap with `put_QueryMaxResults`. Windows 10/11 search indexing is on by default for user folders.

### 3. Backdrop (Win11 = documented, Win10 = legacy)
- **Windows 11 22H2+:** `DwmSetWindowAttribute(hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &(int)DWMSBT_TRANSIENTWINDOW /*3*/, sizeof(int))` — gives Desktop Acrylic behind the whole window. Also requires the window to be transparent: QML `Window { flags: Qt.FramelessWindowHint | Qt.Window; color: "transparent" }` + `QWindow::setFlag(Qt::WA_TranslucentBackground)` (via `QQuickWindow`/`QWindow` API). Verified: enum requires `Windows 11 Build 22621`.
- **Windows 10:** `SetWindowCompositionAttribute(hwnd, {WCA_ACCENT_POLICY=19, ACCENT_ENABLE_BLURBEHIND=3})` — undocumented enum values, now-documented-but-discouraged function, known drag-lag since 1903. Acceptable for a launcher (no window dragging!) — a launcher popup isn't dragged/resized, sidestepping the classic lag complaint.
- **Design for graceful degradation:** if both calls fail (remote sessions, exotic configs), render the solid dark theme background — the launcher must still look intentional.

---

## Installation (Qt project setup notes for Windows)

```powershell
# 1. Install Qt 6.11.1 open-source via the online installer (MSVC 2022 x64)
#    Components: Qt Quick, Qt Core, Qt Gui, Qt Widgets, Qt SQL, Qt Shader Tools
# 2. Install Visual Studio 2022 Build Tools / VS2022 with "Desktop development with C++"
# 3. Add C++/WinRT: NuGet package Microsoft.Windows.CppWinRT (or use Windows SDK's headers)

# CMakeLists.txt essentials
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Widgets Sql)
qt_standard_project_setup()          # Qt 6.3+: sets AUTOMOC/AUTORCC, enables qt_add_executable rules
qt_add_executable(rofiwin WIN32 main.cpp ...)
qt_add_qml_module(rofiwin URI RofiWin VERSION 1.0 QML_FILES ...)   # 6.2+: wires qmllint/qmlcachegen
target_link_libraries(rofiwin PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Widgets Qt6::Sql)

# C++/WinRT in CMake
find_package(Microsoft.Windows.CppWinRT REQUIRED)   # after FetchContent or NuGet restore
target_link_libraries(rofiwin PRIVATE Microsoft.Windows.CppWinRT)

# Deployment (release pipeline)
windeployqt --qmldir <qml-src-dir> --release <build>\rofiwin.exe
# → produces a self-contained folder; bundle VC_redist.x64.exe, NOT the copied compiler DLLs

# Installer (NSIS)
makensis installer.nsi   # per-user install, Start Menu shortcut, optional Run-key autostart
```

**Linker/runtime flags:** `WIN32` subsystem (no console), x64 only for v1. Enable `-O2` + LTCG for release; QML is JIT/SSA-compiled via `qmlcachegen` through `qt_add_qml_module`, which the default setup handles.

---

## Alternatives Considered

| Category | Recommended | Alternative | Why Not |
|---|---|---|---|
| Qt version | 6.11.1 open source | 6.8 LTS | LTS patch releases ≥6.8.4 are **commercial-only** — an LGPL project would be stuck at 6.8.3 with known CVEs (6.10 line already has public CVE patches). 6.11.1 is current with standard support to 2027-03-17. |
| Installer | NSIS 3.x (per-user EXE) | WiX (MSI) | WiX has a steep learning curve and incomplete docs; MSI buys enterprise deployment we don't need. NSIS is what the Qt ecosystem actually ships (and has a UAC plugin for elevation prompts). See comparison below. |
| Installer (later) | MSIX (optional) | — | MSIX gives Store distribution + clean uninstall, but requires code signing for sideload, package-identity constraints, and it fights Qt's classic "write next to exe" patterns. `windeployqt --appx` exists when we get there. Keep for post-v1. |
| Icon extraction | IShellItemImageFactory | SHGetFileInfo | SHGetFileInfo is legacy, caps at 32/48px sizes and misbehaves on per-monitor DPI. IShellItemImageFactory returns arbitrary-size HBITMAPs. Keep SHGetFileInfo only as a fallback path. |
| Hotkey | Raw RegisterHotKey + QAbstractNativeEventFilter | QHotkey library | Same mechanism underneath; raw avoids a dependency and gives direct conflict-handling (RegisterHotKey's FALSE return). |
| Search backend | Windows Search index (COM) | Everything SDK, folder walking | Already decided by project (PROJECT.md). Everything's SDK is closed-source/freeware and indexes NTFS only; folder walking misses content search. Windows Search is built-in and COM-documented. |
| App index storage | In-memory (worker thread) | SQLite (Qt SQL) | Sub-100ms enumeration doesn't need a DB; SQLite adds deploy weight (Qt SQL plugin) for zero v1 benefit. YAGNI. |
| Qt linkage | Dynamic (LGPL) | Static Qt build | Static linking converts the app into an LGPL "combined work" — forces GPL-style source obligations. Dynamic linking keeps app source closed. Mandatory for this project. |
| Backdrop | DWM system backdrop (Win11) | QtGraphicalEffects FastBlur / MultiEffect in-window blur | QML blur blurs QML content — it cannot blur the **desktop behind** the window. Wrong tool for acrylic. (MultiEffect is still the right tool for blurring QML content inside the popup.) |

### Installer comparison: NSIS vs WiX vs MSIX (2026 status)

| Criterion | NSIS (rec) | WiX | MSIX |
|---|---|---|---|
| Per-user install, no admin | Easy (UAC plugin) | Painful (documented pain points) | Built-in |
| Learning curve | Low-mid | Steep, sparse docs | Mid (manifest + signing) |
| Code signing required | Recommended, not required | Recommended | **Required** to sideload |
| Store/Winget distribution | Manual (winget manifest) | Manual | Native |
| Qt fit (windeployqt output) | Just bundle the folder | MSM/heat tooling friction | `--appx` flag exists; sandbox quirks |
| Suits this project? | **Yes — v1** | No | Post-v1 (Store) |

---

## What NOT to Use

| Item | Why Not |
|---|---|
| **Qt 6.8 LTS** | Commercial-only patch releases (6.8.4+) — no open-source security updates. Confirmed in Qt's official supported-versions table. |
| **Static Qt linking / aqtinstall static builds** | Violates the LGPL "dynamically linked = proprietary app OK" carve-out; forces GPL obligations. |
| **QtGraphicalEffects (Qt5Compat)** | Deprecated in Qt 6; Qt docs point to MultiEffect for in-QML blur. (And it can't do desktop backdrop blur anyway.) |
| **UndocumentedWindowEffects (maicol07) package** | It's a **.NET** package for WinForms/WPF — not callable from Qt/C++. The equivalent C++ is a ~100-line `SetWindowCompositionAttribute` wrapper you write once. |
| **QProcess for elevation** | Cannot request UAC elevation — no verb support. ShellExecuteEx `runas` is the only sane path. |
| **SHGetFileInfo as primary icon API** | Legacy, size-limited, DPI-unaware. Fallback only. |
| **Scraping `HKCU\Software\Microsoft\Windows\CurrentVersion\Appx\...` or `%ProgramFiles%\WindowsApps`** | WindowsApps is ACL-locked and layout is internal; registry layout is unreliable. PackageManager is the sanctioned API. |
| **Everything SDK / alternative indexes** | Project decision (PROJECT.md): Windows Search index. Also Everything indexes only NTFS volumes and its SDK licensing is restrictive. |
| **Shipping windeployqt-copied MSVC runtime DLLs** | Qt docs: those DLLs "are not intended or licensed for redistribution". Ship VC_redist.x64.exe. |
| **QSettings-in-INI for settings** | On Windows, QSettings NativeFormat = registry, which is the platform convention and keeps autostart/settings in one place. (Config file is a defensible choice but adds code for no v1 gain.) |
| **Tray icon from QML** | Doesn't exist in Qt Quick. C++ QSystemTrayIcon only. |

---

## Version Compatibility

| Component | Version | Works With |
|---|---|---|
| Qt 6.11.1 | current | Windows 10 1809+ (Qt 6.11 supported platforms). **Qt 6.12 will be the last Qt supporting Windows 10** (official note on Qt for Windows page) — plan the Win10-support horizon; if Win10 support matters past ~2027, pin ≤6.12. |
| Qt 6.10.3 | conservative fallback | Same platforms; standard support ends 2026-10-07 — too soon for a project shipping in 2026/2027, hence 6.11. |
| MSVC 2022 x64 | matches Qt binary ABI | Must match the Qt build's compiler family (both MSVC). |
| C++/WinRT | NuGet 2.x | Windows SDK 10.0.17763+; PackageManager API available since 10240. |
| DWMWA_SYSTEMBACKDROP_TYPE | Win11 build 22621+ | Win10: no documented equivalent → SetWindowCompositionAttribute fallback (see above). |
| RegisterHotKey / WM_HOTKEY | Win Vista+ | MOD_NOREPEAT supported; F12 is reserved by the OS and must not be registered (MS doc). |
| Windows Search COM (searchapi.h) | Win Vista+ | Requires Search indexer service (default-on). |
| NSIS 3.x | installer | Windows 7+; UAC plugin for elevation. |

---

## LGPL Compliance for the Public Release (verified against qt.io obligations page)

Confirmed obligations for an open-source-LGPL Qt app (this is the checklist for the packaging phase):

1. **Dynamic linking only** — ship the standard Qt DLLs; never statically link Qt (QtMain is BSD-exempted, but that's not us). App source stays proprietary — explicitly OK under LGPL "work that uses the library" via dynamic linking.
2. **Deliver Qt's complete corresponding source** — the unmodified Qt 6.11.1 sources are available from code.qt.io; provide a written offer/link (linking to the official Qt source archive satisfies the spirit; include the URL in the license file). No modifications to Qt = nothing to publish beyond the offer.
3. **User must be able to re-link and run** — ship the *unmodified official* Qt DLLs (they can be replaced by recompiled ones). Do not add anti-tamper or signature checks on Qt DLLs (tivoization prohibition).
4. **Provide LGPLv3 license text** + prominent notice (e.g., About dialog entry + license file in the installer). Also include GPLv3 text if any GPL-only module is used (we don't plan to: Qt Core/Gui/Quick/Widgets/Sql are all LGPL; QtQuick3D effects are GPL-only — avoid).
5. No distribution channel terms may restrict these rights (fine for GitHub Releases + NSIS).
6. Per-app analysis is on us: our code is new/own, so no patent or third-party header contamination concerns beyond Qt's own (Qt's BSD-style exceptions cover e.g. QtMain).

---

## Sources

| Source | What It Verified | Confidence |
|---|---|---|
| doc.qt.io/qt-6/qabstractnativeeventfilter.html (Qt 6.11.1) | Filter class name, `windows_dispatcher_MSG` hotkey path, usage pattern | HIGH |
| learn.microsoft.com .../winuser/nf-winuser-registerhotkey | RegisterHotKey semantics, MOD_NOREPEAT, F12 reservation, NULL-hWnd → thread queue | HIGH |
| learn.microsoft.com .../dwmapi/ne-dwmapi-dwm_systembackdrop_type | DWMSBT_TRANSIENTWINDOW=acrylic, minimum Win11 build 22621 | HIGH |
| learn.microsoft.com .../dwm/setwindowcompositionattribute | Function now documented but "not recommended; use DwmSetWindowAttribute instead" | HIGH |
| learn.microsoft.com .../shobjidl_core/nf-shobjidl_core-ishellitemimagefactory-getimage | GetImage signature, SIIGBF flags, UI-thread warning | HIGH |
| learn.microsoft.com .../searchapi/nn-searchapi-isearchqueryhelper | ISearchQueryHelper purpose, acquisition via ISearchCatalogManager::GetQueryHelper | HIGH |
| microsoft.github.io/PowerToys/modules/launcher/plugins/program/ | PowerToys Run Program plugin: PackageManager enumeration + manifest asset icons — real-world confirmation of the app-enumeration approach | HIGH |
| learn.microsoft.com .../windows.management.deployment.packagemanager.findpackagesforuser | FindPackagesForUser/FindPackagesByUserSecurityId, packageQuery capability, SID semantics | HIGH |
| doc.qt.io/qt-6/windows-deployment.html (Qt 6.11) | windeployqt usage incl. `--qmldir`, `--appx`; MSVC redist DLL licensing warning | HIGH |
| doc.qt.io/qt-6/qt-releases.html (Qt 6.11) | Version table: 6.11.1 / 6.10.3 / 6.8.7 "LTS, commercial only" — LTS patch availability | HIGH |
| doc.qt.io/qt-6/windows.html (Qt 6.11) | "Qt 6.12 will be the last version to support Windows 10" | HIGH |
| qt.io/licensing/open-source-lgpl-obligations | LGPL obligations: dynamic linking carve-out, source offer, re-linkability, notice requirements | HIGH |
| learn.microsoft.com (C++/WinRT docs) | C++/WinRT as the WinRT interop approach | MEDIUM (URL moved; API well-established) |
| learn.microsoft.com .../shellapi/nf-shellapi-shellexecuteexw + "runas" verb | ShellExecuteEx elevation verb | MEDIUM (well-established, not re-fetched) |
| learn.microsoft.com .../setupapi/run-and-runonce-registry-keys | HKCU Run key autostart mechanism | MEDIUM (well-established, not re-fetched) |
| learn.microsoft.com .../shobjidl_core/nn-shobjidl_core-ishelllinkw | IShellLink COM parsing of .lnk | MEDIUM (well-established, not re-fetched) |
| GitHub: microsoft/Windows-classic-samples (DSearch sample) | End-to-end Windows Search query reference | MEDIUM |
| NSIS vs WiX community comparisons (doubleSlash 2016 + appmus 2026) | Installer landscape: NSIS pragmatism, WiX learning curve, UAC plugin need | MEDIUM |
| selastingeorge/Win32-Acrylic-Effect README | SetWindowCompositionAttribute acrylic lag history, Win10 blur options | MEDIUM (community, matches MS "not recommended" stance) |

**Open flags for later phases:**
- Exact `ISearchQueryHelper` result-row consumption (OLE DB vs ADO in C++) — spike during the search phase; Microsoft's DSearch sample is C# (Microsoft.Search.Interop).
- Per-monitor DPI behavior of `IShellItemImageFactory` on mixed-DPI setups — verify in icon phase.
- QML transparent-window + DwmSetWindowAttribute interaction (blur behind a transparent Qt Quick window can be finicky on some drivers) — spike in UI phase; fallback to solid background is the escape hatch.

---
*Stack research for: Windows application launcher (Qt6 + QML)*
*Researched: 2026-08-09*
