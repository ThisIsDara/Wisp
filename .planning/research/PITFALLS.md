# Pitfalls Research

**Domain:** Windows application launcher (Qt6 + QML)
**Researched:** 2026-08-09
**Confidence:** HIGH (official MS/Qt docs + PowerToys Run source), MEDIUM where noted (forums/single-source)

## Critical Pitfalls

### Pitfall 1: Global hotkey silently fails to register (RegisterHotKey returns FALSE)

**What goes wrong:** The launcher's hotkey (default Alt+Space) does nothing. App appears "installed and running" but the user's muscle memory gets nothing. Worst case: it works on the dev machine but not on user machines.

**Why it happens:**
- The key combination is already registered by another process (e.g., a different launcher, a game's overlay, PowerToys, or OEM software). `RegisterHotKey` fails with `ERROR_HOTKEY_ALREADY_REGISTERED` when the keystrokes are already registered for another hotkey.
- F12 is reserved by the kernel debugger at all times — registering it fails or hijacks.
- The ID passed is outside the application range (0x0000–0xBFFF) or collides with a shared-DLL range.
- The call happens on a thread that has no message loop — `WM_HOTKEY` is posted to the calling thread's queue and is silently dropped.
- Calling `RegisterHotKey` before `QGuiApplication` fully initializes its native window — Qt's own message handling can interfere.

**How to avoid:**
- Check the return value and `GetLastError()`; on failure, surface a tray notification: "Hotkey Alt+Space is in use by another app. Change it in settings." Never fail silently.
- Register with `MOD_NOREPEAT` so a held hotkey doesn't fire repeatedly (Windows Vista+).
- Re-register on failure after the user changes the hotkey in settings; validate the chosen combo before saving it.
- Register with `hWnd == NULL` (thread-level) and rely on Qt's event loop, or use a dedicated invisible `QWindow` handle — pick one pattern and test on both.
- Do not use the Win key alone (`MOD_WIN`) — reserved for OS use.
- Unregister on app exit (`UnregisterHotKey`) so re-launches don't accumulate registrations.

**Warning signs:** Hotkey never works after launch; works first time but not after tray restart; hotkey works on dev PC but user reports it dead; `GetLastError() == 1409` (ERROR_HOTKEY_ALREADY_REGISTERED).

**Phase to address:** Hotkey/window-toggle phase — the hotkey must be configurable with conflict detection from the start. Re-verify in packaging phase on a machine with another launcher (e.g., PowerToys Run) installed.

### Pitfall 2: Launcher steals focus from fullscreen games / windows it shouldn't

**What goes wrong:** User presses hotkey mid-game; the launcher window appears but the game minimizes or the foreground app loses keyboard/mouse capture. This is the #1 "my launcher is obnoxious" complaint pattern (see SoundSwitch banner regressions).

**Why it happens:**
- Showing and activating a window triggers `SetForegroundWindow`-style activation that kicks the foreground app (especially exclusive-fullscreen D3D apps, which minimize on focus loss).
- Qt's `show()` + `raise()` + `requestActivate()` sequence activates the window by default.
- `WS_EX_NOACTIVATE` alone is insufficient: Windows' "activate on mouse hover" setting sends `WM_MOUSEACTIVATE`, and `DefWindowProc` answers "yes, activate me" — the style is bypassed (Raymond Chen documented this).
- Any post-show z-order refresh (`BringToFront()`, `TopMost` toggle) re-triggers activation-prone paths.

**How to avoid:**
- Combine, in C++ window wrapper: `WS_EX_NOACTIVATE | WS_EX_TOPMOST` extended styles, and handle `WM_MOUSEACTIVATE` returning `MA_NOACTIVATE` (or `MA_NOACTIVATEANDEAT`).
- For topmost-without-focus, use `SetWindowPos(hwnd, HWND_TOPMOST, ..., SWP_NOACTIVATE | SWP_SHOWWINDOW)` instead of `raise()`/`BringToFront()`.
- Intercept `WM_WINDOWPOSCHANGING` and force `SWP_NOACTIVATE` into the WINDOWPOS flags to cover internal/system-triggered position changes.
- Query `SHQueryUserNotificationState()` before showing; if `QUNS_RUNNING_D3D_FULL_SCREEN`, consider deferring/delaying the popup until the game exits fullscreen (or at least don't fight the user — show when they next press the hotkey).
- **Decision needed:** Rofi-style launchers DO need keyboard input on show, so `WS_EX_NOACTIVATE` may be wrong for the whole lifecycle. The correct pattern is: activate normally on show (the user asked for it via hotkey), but never re-activate after that, and always use `SWP_NOACTIVATE` for z-order maintenance. Test against exclusive-fullscreen game (e.g., CS2) and borderless windowed.

**Warning signs:** Game minimizes when hotkey pressed; window appears behind the active app; mouse hover activates the launcher; clicking launcher background passes clicks through unexpectedly.

**Phase to address:** Hotkey/window-toggle phase, with explicit test scenario: "launch launcher over exclusive-fullscreen game, game must not minimize, launcher must receive typing."

### Pitfall 3: Launcher shows but doesn't receive keyboard input on first show

**What goes wrong:** The window appears centered, animation plays, but typing does nothing — the user must click the search box first. Breaks the "hotkey → type → Enter" muscle memory entirely.

**Why it happens:**
- QML focus is scoped: setting `focus: true` on the TextField inside a component doesn't make it active if the window wasn't activated; `activeFocus` requires both window activation AND item-level focus.
- `requestActivate()` is a *request* — Windows may deny it when the process wasn't the foreground process (anti-focus-stealing policy), especially on first show from a tray-resident process.
- The window may not be visible/ready when `requestActivate()` is called (activation during startup/creation is ignored).
- `forceActiveFocus()` without window activation gives the item focus but the window still isn't active — key events go nowhere.

**How to avoid:**
- On the C++ side: `win->show(); win->raise(); win->requestActivate();` in that order, deferred with `QTimer::singleShot(0, ...)` or on the `visibleChanged` signal, not during construction.
- On the QML side: put the search field in a `FocusScope`; call `searchField.forceActiveFocus(Qt.TabFocusReason)` in the `onVisibleChanged`/`onActiveChanged` handlers, and again when `Window.active` flips to true.
- Windows foreground rules: since the user just pressed our hotkey, we're the foreground process — but only if `WM_HOTKEY` is processed promptly. Don't do app enumeration or index queries synchronously in the hotkey handler before showing the window.
- If activation still fails, fall back to `SetForegroundWindow(hwnd)` (allowed since the hotkey press is a user input).

**Warning signs:** Window renders but typing does nothing on first show; works on second show; works after a mouse click anywhere; buttons need double-click to activate.

**Phase to address:** Hotkey/window-toggle phase — acceptance criteria must include "type immediately after hotkey press, first time, no click".

### Pitfall 4: UWP app enumeration misses apps or lists junk (system internals, frameworks, duplicates)

**What goes wrong:** Store apps missing from results, or garbage entries ("Settings", "Windows Security" sub-apps, duplicate names, apps that error on launch). PowerToys Run's program plugin exists precisely because naive enumeration is wrong.

**Why it happens:**
- Enumerating `C:\Program Files\WindowsApps` directly: directory access is ACL-restricted, and packages are per-user registered — a path listing is both incomplete and unreadable.
- `Get-AppxPackage`-style enumeration (`PackageManager.FindPackagesForCurrentUser()`) returns framework packages, stub packages, and packages with no start-entry apps; each package has 0..n `Application` elements in `AppxManifest.xml`.
- Launching an `Application` whose `AppListEntry == "none"` fails or opens a settings page.
- Confusing **App Execution Aliases** with launchable targets: aliases in `%LOCALAPPDATA%\Microsoft\WindowsApps` are 0-byte reparse points (`IO_REPARSE_TAG_APPEXECLINK`) — they cannot be executed via `CreateProcess` directly; they go through the AppInfo activation service.

**How to avoid:**
- Use the `PackageManager` API (`FindPackagesForCurrentUser`) → `InstalledLocation` → parse `AppxManifest.xml` for each `<Application>` entry. Build AUMID as `PackageFamilyName + "!" + Application.Id` (this IS the launch key).
- Filter like PowerToys Run's `UWP.cs`: drop packages where `IsFramework` is true, `InstalledLocation` empty, or manifest unreadable; drop apps with empty `UserModelId`/`DisplayName` or `AppListEntry == "none"`.
- De-duplicate against Start Menu `.lnk` shortcuts: `.lnk` targets resolve to a real exe, AUMID apps to `shell:AppsFolder\<AUMID>` — a store app may appear in both lists; key by a stable id.
- Launch UWP via `IApplicationActivationManager::ActivateApplication(AUMID, ...)` or `ShellExecute("shell:AppsFolder\\" + AUMID)`.
- Subscribe to `PackageCatalog` install/uninstall events if you want live updates (PowerToys does this); otherwise re-enumerate at each show or on a background refresh.

**Warning signs:** Store apps missing from search; searching "settings" returns a non-functional entry; launcher crashes or hangs reading WindowsApps; apps launch to a black screen.

**Phase to address:** App enumeration phase. Include a "junk filter" test list: expected items (Calculator, Terminal, Notepad), excluded items (package frameworks, `AppListEntry=none` apps), and per-user vs all-users packages.

### Pitfall 5: Windows Search queries return stale or empty results (or block the UI)

**What goes wrong:** File search shows nothing for files the user knows exist; or results are stale (deleted files still listed); or the first keystroke freezes the UI while a query runs.

**Why it happens:**
- Hand-rolled SQL against the index: escaping failures, `CONTAINS` syntax errors, no ranking — SQL must be generated, never hand-built.
- Cached `ISearchQueryHelper`/connection string goes stale as the catalog evolves.
- Indexer disabled/paused on the machine ("optimizer" tools, Game Mode, admin policy) → queries return empty with no error.
- Index is being built on a fresh/clean machine → queries return partial or empty results for hours.
- Locale mismatch: `GenerateSQLFromUserQuery` uses regional locale settings while other `ISearchQueryHelper` calls don't — inconsistent date parsing etc.
- OLE DB `Execute` calls run synchronously on the UI thread → multi-second freezes.

**How to avoid:**
- Get a fresh `ISearchQueryHelper` via `ISearchCatalogManager::GetQueryHelper` for the `SystemIndex` catalog at query time (don't cache it), use `GenerateSQLFromUserQuery` for AQS→SQL conversion, and add `WHERE` restrictions via `put_QueryWhereRestrictions` (e.g., scope to `System.ItemUrl LIKE 'file:%'`, exclude `System.Search.AutoSummary` junk).
- Check `ISearchCatalogManager::GetCatalogStatus` — handle `CATALOG_STATUS_SHUTTING_DOWN` and empty results when status isn't IDLE; surface "Indexer still building index" instead of "no results".
- Run queries on a worker thread (OLE DB COM init per thread, `CoInitializeEx`); push results to the UI via signals. Consider debouncing (150–200ms) so every keystroke doesn't spawn a query.
- Set `put_QueryMaxResults` (e.g., 30) — the default is unlimited.
- Escape `'` in user input; let the helper generate SQL rather than string-concatenating.
- Content query: use both `CONTAINS` (prefix expansion via `SEARCH_TERM_PREFIX_ALL`, which is default) and FREETEXT for ranking, as the helper does automatically.

**Warning signs:** File search always empty while Start-menu search works; first query blocks UI; results include files deleted days ago; results differ across locales; empty results on machines where "Indexing Options" shows "Indexing complete" as not-yet-true.

**Phase to address:** File search phase — with explicit fallback UX and a clean-VM test where indexing is still in progress.

### Pitfall 6: Icon extraction blocks the UI or yields wrong/missing icons (especially UWP)

**What goes wrong:** Launcher freezes on open while icons resolve; UWP apps show generic icons; icons are tiny/blurry on high-DPI; startup takes seconds because every app's icon is extracted synchronously.

**Why it happens:**
- `SHGetFileInfo`/`IExtractIcon::Extract` on the UI thread — shell icon extraction can hit the disk, network shares, or hung shell extensions (measured seconds).
- UWP app icons aren't files: manifest `Logo`/`Icon` URIs are indirect strings like `@{Microsoft.Windows.Photos_...?ms-resource://.../PhotosAppList.png}` — `ExtractIcon`/`SHGetFileInfo` return FILE_NOT_FOUND.
- UWP asset files are multi-variant (`scale-100/125/150/200/400`, `targetsize-NN`, `_contrast-white`), and picking the wrong variant gives blurry or missing icons.
- Shell icon APIs have threading requirements: COM must be initialized on the worker thread; some shell calls need STA + message pumping.

**How to avoid:**
- Never call icon extraction on the UI thread. Two sanctioned patterns: (a) `IShellItemImageFactory::GetImage` with `SIIGBF_INCACHEONLY` on the UI thread, then fall back to a background thread with full extraction when it fails; (b) full extraction on a worker thread (COM-init, STA) and ship the `HBITMAP`/`QPixmap` back via signal. Cache everything (icon cache keyed by app id + size + theme).
- For UWP icons: parse the manifest `Logo` URI → `SHLoadIndirectString` to resolve the indirect string to a real path → pick the best `scale-*` variant for the current `devicePixelRatio` (PowerToys' `SetScaleIcons`/`SetTargetSizeIcon` logic is the reference implementation).
- Resolve icons lazily: show the result list immediately with placeholder icons, populate as extraction completes (QML `Image` with a placeholder + update). This keeps open-to-type latency at ~0ms.
- Use the system icon cache size appropriate to DPI (e.g., 32px logical → extract at dpr*32 and downscale).

**Warning signs:** First open after boot freezes; UWP entries show blank/generic icons; icons look wrong after display scaling changes; icon cache grows without bound (memory leak pattern).

**Phase to address:** Icon phase (after app enumeration). Must include: UWP indirect-string resolution, DPI-variant selection, worker-thread extraction, cache eviction.

### Pitfall 7: Blurry or mis-scaled UI on mixed-DPI monitors; DPI awareness set wrong or too late

**What goes wrong:** Blurry text/rounded corners on secondary monitors; window wrong size when moved between monitors; scaled screenshot captures; `devicePixelRatio` mismatches making `x/dpr` math wrong.

**Why it happens:**
- Another component/library calls `SetProcessDpiAwareness`/`SetProcessDpiAwarenessContext` first with a different value → Qt's default PMv2 call fails with `E_ACCESSDENIED` (0x5) and Qt falls back — or your own C++ code calls DPI APIs after Qt already set awareness (Qt sets it before `main()` in the QPA init).
- Mixing non-DPI-aware Win32 calls (`GetSystemMetrics`, hardcoded pixel sizes) with Qt's logical coordinate system — coordinates from raw `lParam` (physical pixels) vs `QCursor::pos()` (logical) disagree across differently-scaled monitors.
- Caching DPI/scale values at startup; PMv2 delivers `WM_DPICHANGED` when moving monitors, and stale caches break layout.

**How to avoid:**
- Qt 6 defaults to **Per-Monitor DPI Aware V2** — do NOT call `SetProcessDpiAwareness*` yourself anywhere (including in the launcher's own code); let Qt own it. Never call `QApplication::setHighDpiScaleFactorRoundingPolicy` after window creation. If a third-party library (e.g., screeninfo) forces awareness, fix it at the source.
- Always use Qt APIs for geometry: `QWindow::devicePixelRatio()` (per-window, tracks monitor moves), `QGuiApplication::screens()`, `QScreen::availableGeometry()`. Never use `GetSystemMetrics` for layout.
- If you must read raw Win32 coordinates (e.g., in `nativeEvent` for WM_HOTKEY/cursor), divide by the *window's* dpr before mixing with QML coordinates, or prefer `QCursor::pos()` (Qt logical).
- For custom-rendered content (icons, effects), render at `dpr` multiples.
- Test on a 2-monitor setup with different scale factors (e.g., 100% + 150%), moving the launcher between them while open.

**Warning signs:** "qt.qpa.window: SetProcessDpiAwarenessContext failed: Access is denied" in logs; blurry UI on secondary monitor; launcher partially offscreen; wrong hit-testing in corner cases; screenshots scaled differently from on-screen.

**Phase to address:** Core shell phase (set up correctly day one — retrofitting DPI is painful), re-verify in icon/polish phase (icon sizes at dpr) and packaging phase (different hardware).

### Pitfall 8: LGPL compliance deferred until release — then discovered too late

**What goes wrong:** Public release blocked because the Qt licensing requirements weren't baked in: statically linked Qt (or a GPL-only Qt module), missing license text, no source offer, no relinking instructions. Failing this is a release-blocker, not a bug.

**Why it happens:**
- Static linking for convenience (smaller distribution) converts the app from "work that uses the library" to part of a combined work — the app's own source becomes subject to LGPL obligations.
- Forgetting that the *library's* source must be offered: LGPLv3 requires providing the Qt source (or a written offer valid 3+ years) *in the same way you distribute the binary* — a link to Qt's servers is not sufficient; you must control the source offer.
- Shipping Qt DLLs without license text / prominent notices; Qt's own docs require: license copy + prominent notice + (for combined works) copyright notices shown at runtime.

**How to avoid:**
- Decide at skeleton phase: **dynamic linking only** (default for windeployqt), and lock it with a CI check that the build uses `-DBUILD_SHARED_LIBS` style config / .dll outputs — no `Qt6::Core` static artifacts.
- Use only LGPL-compatible Qt modules (QtCore/QtGui/QtQuick are fine; some add-on modules are GPL-only — check before adding, e.g., certain tooling modules; QtMain is BSD so that's fine).
- Create a `THIRD-PARTY-NOTICES.txt` (LGPLv3 text + list of Qt modules + exact Qt version) shipped in the installer root, plus an in-app "About → Licenses" entry; ship a written source offer with a URL you control that hosts the exact Qt version source (or bundle it).
- Verify relinkability: user must be able to replace `Qt6*.dll` with their own build. Test this once in the packaging phase (swap a debug-built Qt6Core.dll and confirm the app runs).

**Warning signs:** Build config shows static Qt; no LICENSE/THIRD-PARTY file in staging; "Qt" not mentioned anywhere in the app; the team can't say which Qt version/build configuration is shipped.

**Phase to address:** Core shell (decision + notices scaffold) and packaging phase (final verification, source offer hosting). Legal review is the user's call — we just make the compliance artifacts first-class deliverables.

### Pitfall 9: Packaging breaks on machines without the dev environment (windeployqt gaps)

**What goes wrong:** Installed app fails to start with "Qt6Quick.dll not found", "module QtQuick.Controls is not installed", "The code execution cannot proceed because VCRUNTIME140.dll was not found" — or the classic: silent exit with no error at all.

**Why it happens:**
- `windeployqt` doesn't inspect your binary for QML imports — without `--qmldir <path-to-your-qml-sources>`, it omits the `qml/QtQuick`, `QtQuick.Controls` etc. modules. Works in Qt Creator (which has its own QML path), dies on target. **This is the single most common Qt6-on-Windows deployment failure.**
- `windeployqt` cannot see plugins loaded at runtime (image formats, styles) — "blank window" or "image doesn't display" symptoms.
- VC runtime: shipping the compiler's individual `msvcp140.dll`/`vcruntime140_1.dll` from the dev machine (unlicensed for redistribution) instead of the official `vc_redist.x64.exe`; or windeployqt not producing `vc_redist` because the toolchain env (vcvars) wasn't loaded.
- Known windeployqt version bugs (e.g., Qt 6.7.2 missing MinGW libs) — check the version's quirks.
- Forgetting `qt.conf`/plugin path issues when installing to a non-default directory.

**How to avoid:**
- Standardize the deploy command in CI from day one: `windeployqt --release --qmldir <qml sources> <exe>` run from a `vcvars64.bat`-initialized shell, with `--dry-run`/`--verbose 2` output reviewed.
- Test the packaged output on a **clean Windows 10 and Windows 11 VM** (no Qt, no VS, no dev tools) as a release gate — the only reliable way to catch missing DLLs/modules. Include a smoke test: app launches, hotkey works, one app launch, one file search.
- For the VC redist, either run `vc_redist.x64.exe /install /quiet` from the installer (recommended) or chain the official redist in the installer wizard. Never copy msvcp/vcruntime DLLs beside the exe.
- Keep QML files in the exe via qrc (`:/`) or pass the folder to windeployqt; if using qrc, still pass `--qmldir` (it needs the sources to scan imports).

**Warning signs:** Release build runs from Qt Creator but not from the deploy folder; "module ... is not installed" QML errors on target; blank windows on target; missing icon/images at runtime.

**Phase to address:** Packaging phase, but the deploy script and clean-VM checklist should exist from the first release build (Phase: core shell). Don't discover deployment gaps in the final phase.

### Pitfall 10: Autostart registry entry broken (unquoted path, wrong hive, admin context)

**What goes wrong:** Launcher doesn't start on login; Task Manager's Startup tab shows a junk entry named "Program" or "Rofi" with blank icon; launcher starts but its tray/behavior is wrong (e.g., tries to write files it can't).

**Why it happens:**
- Run-key value written without quotes around a path containing spaces: `C:\Program Files\Rofi-Windows\rofi.exe --tray` → Task Manager parses the first token as the program name ("Program"). CreateProcess autocorrects, but the app still misbehaves, and users see the broken entry.
- Using `HKLM\...\Run` (requires admin, runs for all users, and on some setups needs elevation) vs `HKCU\...\Run` (per-user, no admin, correct for a per-user launcher).
- Apps launched from Run keys run with the logged-in user's token — code that assumes "same as double-click" is usually fine, but code that assumes admin (writing to Program Files, HKLM) fails silently at login.
- Writing to the Run key while the app runs at startup ("write to the key during its execution") — the docs explicitly warn against apps recreating entries during their own Run-key execution.

**How to avoid:**
- Write `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` with value `"C:\path\to\rofi.exe" --autostart` — always quote the exe path, keep the value name clean ("RofiWindows").
- Never touch HKLM; document that autostart is per-user (matches tray-app convention).
- Add an `--autostart` arg so the app can behave differently on login (e.g., skip first-run wizard, show tray quietly); read it before showing any window.
- Verify in packaging tests: enable autostart → log out/in → app running; Task Manager Startup shows a proper name, icon, and publisher (set VERSIONINFO CompanyName/ProductName in the exe resource).
- Optional: register a logon-trigger Scheduled Task instead for more control (delayed start), but the Run key is the expected convention for launchers.

**Warning signs:** Task Manager startup entry named just "Program" or blank; launcher not running after login on user machines but works on dev machine (no spaces in dev path); files written to wrong location on login start.

**Phase to address:** Settings/autostart phase + packaging verification.

### Pitfall 11: First launch on a clean machine looks broken (empty everything)

**What goes wrong:** Fresh install → hotkey → search box → typing "spotify" gives nothing; typing a filename gives nothing. The launcher's core value proposition is dead on arrival for the most important users: new users.

**Why it happens:**
- Windows Search index is empty/building for the first minutes-to-hours on a fresh machine (or "Classic" indexing scope only covers Documents/Pictures/Music/Desktop — files elsewhere are never indexed).
- The Windows Search service may be disabled by third-party "optimizers" or by policy.
- App enumeration cache is empty until the first background scan completes (PowerToys Run's program plugin has a measured ~2.7s init — and that's after it's been running).

**How to avoid:**
- Distinguish "no results because nothing matches" from "no results because the source is unavailable": check catalog status (`GetCatalogStatus`), indexer service state, and cache-build state; show a distinct, friendly message ("Search index is still being built — results will improve", "Windows Search is disabled — enable it in Services or search apps only").
- Design the UI so the app list works instantly even when file search can't: app search must never depend on the indexer; it should be backed by its own cache built at first run (background thread, persisted to disk, refreshed on package/start-menu change events).
- On first run, show a brief "building app list…" state with a progress hint rather than an empty list; preload the cache during the installer or first autostart launch.
- For file search, consider a fallback quick scan of the current directory / Desktop when the indexer is unavailable (documented as degraded mode).

**Warning signs:** Fresh VM install → first search empty; user machines with "optimizer" tools report empty file search; app list appears only after several seconds on first run.

**Phase to address:** First-launch experience phase (or explicitly in file-search + app-enumeration phases). This is a UX + diagnostics feature, not a bug-fix.

### Pitfall 12: Qt6 QML effects/backdrop (blur) wrong or unperformant — Qt5 muscle memory

**What goes wrong:** The "blur/transparency backdrop" requirement is implemented with `QtGraphicalEffects` (FastBlur etc.), which is Qt5-era: in Qt6 it only exists in the deprecated `Qt5Compat` module; on some builds it drags in the compat module or fails to render. Or the blur animates at 15fps, killing the 60fps promise.

**Why it happens:**
- Tutorials and muscle memory from Qt5 say `import QtGraphicalEffects` — Qt6 removed it from the default modules (Qt 6.5+ story is `QtQuick.Effects`/`MultiEffect`).
- Blur is expensive: each frame re-blurs the whole background; blurring an animating source forces re-blur every frame; full-screen blur at 4K is a GPU killer.
- Layered-window transparency (`Qt.WA_TranslucentBackground`-style) combined with blur has its own cost and edge cases (no DWM blur-behind API for arbitrary windows).

**How to avoid:**
- Use `MultiEffect` (import `QtQuick.Effects`, Qt 6.5+): blur + mask + colorization in one shader pass; `blurEnabled` toggle; set `visible: false` when effect unused; `autoPaddingEnabled: false` when blurring full-bleed.
- Blur a *static* offscreen layer (e.g., a grabbed/static background snapshot or the masked background layer), not a live animating subtree; re-blur only when content changes, not per-frame.
- Consider cheaper backdrop: translucent panel + subtle contrast (Rofi itself doesn't blur); make blur an optional setting with a performance guard (disable on weak GPUs or battery).
- Never import `Qt5Compat.GraphicalEffects` for new code.

**Warning signs:** Effect doesn't render on some machines; blur stutters during open animation; Qt5Compat module appearing in windeployqt output; high GPU usage while idle.

**Phase to address:** UI/polish phase (animation + backdrop). Spike the blur approach early in that phase, before wiring the theme system.

### Pitfall 13: Launching apps wrong (CreateProcess where ShellExecute is required; UWP launch failure)

**What goes wrong:** Launching an admin-required app (e.g., a portable utility with requireAdministrator manifest) does nothing or silently fails; UWP apps fail to activate; some apps launch with the wrong working directory.

**Why it happens:**
- `CreateProcess` does not handle UAC elevation: it fails with `ERROR_ELEVATION_REQUIRED` (740) for apps whose manifest requests elevation; only `ShellExecute`/`ShellExecuteEx` (via the shell's AppInfo service) can trigger the UAC prompt.
- `ShellExecute` on `shell:AppsFolder\<AUMID>` is the reliable UWP path, but `CreateProcess` on the `WindowsApps` folder path fails (ACL) or on an app-execution-alias path launches via the wrong mechanism.
- Working directory defaults to the launcher's directory (or "Start in" of the .lnk) — some apps misbehave (relative config paths, DLL search) when started from the launcher's cwd.
- Launching a `requireAdministrator` app directly with CreateProcess also bypasses the shield prompt — the user thinks the launcher is broken.

**How to avoid:**
- Launch everything through `ShellExecuteEx` with `lpVerb = "open"` (or "runas" only when the user explicitly picks "Run as administrator" from a context menu), `lpDirectory` = the target's directory (for .lnk, use the shortcut's Start-In; for exe, its parent dir). Keep the launcher's own cwd out of it.
- UWP: `ShellExecuteEx("shell:AppsFolder\\" + AUMID)` or `IApplicationActivationManager::ActivateApplication`.
- Don't block the UI while launching: use `QProcess`/`ShellExecuteEx` async or fire-and-forget; the launcher should hide immediately on Enter.
- Never run the launcher itself elevated (asInvoker manifest) — an elevated launcher would silently bypass UAC prompts for children, and its Run-key autostart breaks for standard users.

**Warning signs:** Some apps won't start from launcher but start from Start menu; UAC prompts appear twice or never; apps open in the wrong folder; elevated launcher in Task Manager with UAC shield.

**Phase to address:** Launch logic — part of the first vertical slice (app enumeration phase includes launch), refined with "Run as administrator" in settings phase.

### Pitfall 14: Startup latency — app list built synchronously on first show

**What goes wrong:** First hotkey press after login takes 2–5s (PowerToys Run's program plugin: ~2.7s measured). User perceives the launcher as broken/slow and uninstalls. The core value is "instant".

**Why it happens:**
- Enumerating Start Menu (thousands of .lnk) + UWP packages + parsing every AppxManifest.xml + resolving icons synchronously in the hotkey path or at startup.
- COM/`PackageManager` enumeration calls are slow on first call (cold).

**How to avoid:**
- Build the app cache asynchronously at startup (tray launch, not hotkey): background thread builds a persisted cache (JSON/SQLite: name, launch command, AUMID, icon path, last-used); hotkey path reads only the cache.
- Show results from cache instantly; refresh cache incrementally in background (package catalog events, start-menu watcher via `ReadDirectoryChangesW`).
- Never do enumeration/parsing inside the `WM_HOTKEY` handler (which also delays window activation → Pitfall 3).

**Warning signs:** First open after boot slow; CPU spike at login; window shows after animation delay; "not responding" during first search.

**Phase to address:** App enumeration phase — performance budget explicit: cache build off the hotkey path, first-show < 100ms.

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Hardcoded `C:\Users\...\AppData\...` or `Program Files` paths | Works on dev machine | Breaks every clean install; installers relocate; per-user vs all-users mismatch | Never — use `QStandardPaths`/`SHGetKnownFolderPath` from day one |
| Icon extraction on UI thread ("it's fast on my machine") | Zero code | Random freezes on other machines (network drives, shell extensions) | Never — worker thread + cache is ~40 lines |
| Re-enumerate apps on every show | Always fresh | Multi-second opens; defeats the product | Only for a manual "refresh" command |
| Hand-built Windows Search SQL | Feels direct | Escaping bugs, no ranking, stale syntax, locale bugs | Never — `GenerateSQLFromUserQuery` exists for this |
| One `ISearchQueryHelper` cached forever | Slightly faster | Stale catalog → missing/phantom results | Acceptable only if re-created per query; don't bother caching |
| Static Qt linking to shrink installer | Smaller download | LGPL obligations pull app source in; relink requirements | Never for public release — commercial license or dynamic only |
| Ship dev-machine msvcp140.dll alongside exe | "Fixes" missing VC runtime | Unlicensed redistribution; breaks after Windows updates | Never — use official vc_redist |
| `visible = true` + `raise()` + pray for focus | Simple | Intermittent no-keyboard-input bugs (Pitfall 3) | Never — follow the activate-sequence pattern |
| Autostart entry without quotes | One less character | Task Manager junk entry; app not starting | Never — quote always |
| Enumerate `WindowsApps` folder directly | No API learning | ACL errors, missing per-user packages, junk entries | Never |
| Store UWP icon URI and resolve at display time every time | Simple | Startup latency; re-resolution failures | Acceptable with cache keyed by package+scale |

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| `RegisterHotKey` + Qt event loop | Calling with an HWND that Qt later destroys, or on a non-loop thread | Register thread-level (`hWnd=NULL`) after app init; unregister on `aboutToQuit`; verify with `GetLastError` on failure |
| Win32 messages vs QML | Reading raw `lParam` coords and mixing with QML `mapFromGlobal` | Use `QCursor::pos()` (logical) for cursor; only use raw coords inside `nativeEvent` divided by window dpr |
| COM (Search/Shell) from C++ | `CoInitialize` never called on worker threads; COM objects freed on wrong thread | `CoInitializeEx(COINIT_APARTMENTTHREADED)` per worker; release COM objects on the same thread that created them; keep `IStream`/`IShellItem` lifetime correct |
| PackageManager API | `FindPackagesForCurrentUser` on a non-STA/worker thread with no COM init → crash/hang | COM-init the worker; wrap package enumeration in try/catch per package (PowerToys pattern) |
| .lnk parsing | Parsing shortcut content by hand (binary format) | `IShellLinkW`/`CoCreateInstance` CLSID_ShellLink to resolve target/args/icon — or use the folder COM enum (`IShellFolder`) |
| windeployqt + qrc QML | Assuming qrc-compiled QML needs no deploy scan | Still pass `--qmldir` — windeployqt scans sources for imports, not the compiled binary |
| DPI + icons | Always loading 32px icons | Pick by `devicePixelRatio`; UWP assets: prefer `scale-<dpr>` variant (PowerToys logic) |
| Elevation | Launching every app with `runas` verb | Only when user explicitly requests admin; default `open`; launcher itself `asInvoker` |
| `SHLoadIndirectString` | Treating `@{...}` URI as a path | Resolve indirect strings before any file API; handle failures (missing scale variant) with placeholder icon |

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|-----------|----------------|
| Synchronous app enumeration on hotkey/show | 1–5s delay on first open | Background cache build at startup; persisted store | Cold machines, many installed apps, network-mapped Start Menu |
| Icon extraction on UI thread | Freezes during typing/opening | Worker threads + icon cache; `SIIGBF_INCACHEONLY` fast path | Machines with slow disks, network shares, hung shell extensions |
| Blur/effects over full window each frame | 15–30fps animation; GPU spike | MultiEffect with static blur source; optional blur; smaller blur region | Weak iGPUs, 4K monitors, battery mode |
| Unbounded search queries per keystroke | Typing feels laggy; indexer load | Debounce 150–200ms + cancel in-flight query | Fast typists; large index; query >10ms |
| Search on UI thread (OLE DB) | UI freezes during query | Worker thread + `put_QueryMaxResults(30)` | Large indexes, first query after boot |
| Fuzzy matching over 10k+ items with naive O(n*m) per keystroke | Keystroke latency | Index app names once (precomputed lowercase/keymap), limit candidate set (prefix prefilter), batch scoring in chunks | Large app lists, slow CPUs, IME input |
| Cached-but-never-invalidated app/icon store | Launched exe paths stale (apps moved) | Version the cache; validate on launch failure; refresh events (PackageCatalog, shortcut watcher) | After app updates/uninstalls, PATH changes |
| Loading full-res icons for tiny list rows | Memory bloat | Extract at needed size only (dpr-aware); evict LRU | Long sessions, many results scrolled |

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|-----------|
| Launcher runs elevated (requireAdministrator) | Children launched from it run elevated → UAC bypass; autostart broken for standard users; larger attack surface | `asInvoker` manifest; elevate only on explicit per-app "Run as admin" |
| ShellExecute with unvalidated user-controlled URL/path from search results | Command injection via crafted file names / `http://`/`cmd://` handlers | Launch only resolved app targets (`shell:AppsFolder` AUMIDs, validated exe paths from cache); treat search strings as queries, never as commands |
| Registering a global hotkey with the Win key | Conflicts with OS shortcuts; keylog-adjacent UX | Avoid `MOD_WIN`; document combos; let users rebind |
| Writing autostart entry without escaping/quoting (or letting user config inject registry value data) | Registry value corruption; entry hijack by path tricks | Quote path; validate config values; write via `QSettings` with proper escaping |
| Persisting search cache with full paths + launching blindly | If cache poisoned (app moved), launches wrong file | Validate target exists (and is a file/exe) before launch; re-resolve on failure |
| Storing settings in Program Files | Write failures, VirtualStore redirection, broken on updates | `QStandardPaths::AppConfigLocation` (per-user AppData) |
| Logging queries/paths to a world-readable log | Privacy leak of user's file names | Log to per-user AppData, no file names unless debug mode |

## UX Pitfalls

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| Empty results with no explanation (indexer building/disabled) | "Launcher is broken" → uninstall | Distinguish states: "index still building", "search disabled", "no matches"; show hint text |
| Hotkey conflicts silently ignored | Dead hotkey, user rebinds nothing → abandons app | Tray notification + settings page showing conflict; auto-suggest alternative combo |
| No keyboard input on first show | Must click → broken muscle memory | Pitfall 3 fix; type-test as acceptance criterion |
| Stealing focus from fullscreen games | Users rage-quit the launcher | Fullscreen detection (`SHQueryUserNotificationState`); defer/show later; never minimize the game |
| First-launch delay building list | "It's slow" first impression | Prebuilt cache in installer; progressive disclosure ("indexing apps…") |
| Enter launches while results still updating | Launches wrong item (list shifts under cursor) | Freeze selection on Enter; commit the highlighted item at keypress time |
| Escape doesn't dismiss when IME active | Trapped launcher | Handle `Keys.onEscapePressed` at window level + IME composition edge cases |
| Click-outside doesn't dismiss | Trapped launcher (esp. transparent/rounded window) | Global mouse hook or `Window.active` loss detection (careful: conflicts with WS_EX_NOACTIVATE pattern — decide the dismissal mechanism explicitly) |
| Blur-heavy theme on low-end hardware | Laggy open animation | Detect weak GPU (dpr/GL info) or provide "reduced effects" toggle |

## "Looks Done But Isn't" Checklist

- [ ] **Hotkey registers AND conflicts surface**: test with PowerToys Run / other launchers running; verify failure message + settings recovery path. (Verify: register Alt+Space while PowerToys owns it → must notify, not silently fail)
- [ ] **First-show typing works with zero clicks**: fresh process → hotkey → type → Enter. (Verify: automated keystroke test; also after tray icon re-show)
- [ ] **Fullscreen game immunity**: launch over exclusive-fullscreen game → game must NOT minimize, launcher must accept keys after game exits. (Verify: QUNS state path + manual test)
- [ ] **App list complete but clean**: Calculator, Terminal, Notepad present; no frameworks/junk; no duplicates between .lnk and UWP paths. (Verify: golden-list test on clean VM + PowerToys-style filter unit tests)
- [ ] **UWP launch works end-to-end**: Search "calculator" → Enter → Calculator opens (AUMID path). (Verify: `shell:AppsFolder` resolution on Win10 AND Win11)
- [ ] **File search returns real files with ranking**: known filenames in indexed locations rank above fuzzy garbage. (Verify: query with a term present in 100+ files; confirm CONTAINS+FREETEXT behavior)
- [ ] **Degraded states explained**: indexer disabled / building / paused each produce a distinct, friendly state. (Verify: disable wsearch service → launcher says so; fresh VM → "still building")
- [ ] **Icons correct at 100/125/150/200% scaling, dark theme**: UWP icons resolved via indirect string + scale variant; no placeholders for installed apps. (Verify: sweep every installed app at each dpr)
- [ ] **Mixed-DPI move**: open launcher on 100% monitor → drag to 150% monitor → crisp, correct size, then back. (Verify: window geometry + dpr logging)
- [ ] **Clean-VM launch**: no dev tools, no Qt, no VC runtime → installs, launches, hotkey, launches an app, searches a file. (Verify: Win10 22H2 and Win11 24H2 VMs; check `depends`-style scan for missing DLLs)
- [ ] **Autostart round-trip**: enable → sign out/in → running with tray icon; Task Manager shows proper name/icon/publisher. (Verify: quoted Run key value in regedit)
- [ ] **LGPL artifacts present**: THIRD-PARTY-NOTICES.txt in installer, about dialog credits, source-offer URL live. (Verify: install on clean VM → find and open the file)
- [ ] **App cache survives restarts and updates**: rename a program's folder → old entry either still launches (re-resolve) or is pruned. (Verify: cache invalidation test)

## Recovery Strategies

- **Hotkey dead after an update / another app claims it**: on every startup, if the configured hotkey fails to register, show a tray balloon with a "change hotkey" shortcut into settings — never silently degrade.
- **Empty app list after first-run cache failure**: if the cache build fails (permissions, COM failure), fall back to a synchronous minimal enumeration (Start Menu only) so something works, and log the failure for diagnostics; never show a permanently empty list.
- **Indexer unavailable (disabled/paused)**: fall back to instant directory scans for the current folder/Desktop for file queries (documented degraded mode) — or clearly tell the user file search needs the indexer, and provide a "recheck" affordance.
- **Launch fails with stale cache entry (exe moved)**: on `ShellExecuteEx` failure with ERROR_FILE_NOT_FOUND, remove the entry from the cache and show "app not found — removed from list".
- **Blurry/DPI regressions after display changes**: listen to `QWindow::screenChanged` + `WM_DPICHANGED`; rebuild icon pixmaps and window size for the new dpr; never persist physical sizes.
- **Packaging regression on new Qt patch**: keep the deploy script + clean-VM smoke test scripted in CI so a Qt upgrade's windeployqt quirks are caught the same day.

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Hotkey registration failures | Hotkey/window phase | Conflict test with PowerToys Run; GetLastError logging |
| Focus stealing (games/fullscreen) | Hotkey/window phase | Fullscreen-game manual test in UAT |
| No keyboard input on first show | Hotkey/window phase | First-show typing UAT criteria |
| UWP enumeration junk/misses | App enumeration phase | Golden-list + filter unit tests |
| Search stale/empty/blocking | File search phase | Catalog-status handling; worker-thread queries; clean-VM index-building test |
| Icon extraction/blocking/UWP icons | Icon phase (post-enumeration) | Sweep at 4 dpr levels; worker-thread audit |
| DPI blurriness/mixed monitors | Core shell (day 1) + icon/polish | Multi-monitor mixed-DPI test |
| LGPL compliance | Core shell (scaffold) + packaging (verify) | Compliance checklist in release gate |
| Packaging gaps (windeployqt/VC) | Packaging phase (script from first release build) | Clean-VM install test per release |
| Autostart registry mistakes | Settings/autostart phase | Sign-out/sign-in + Task Manager check |
| First-launch empty experience | First-launch polish (or file-search/app-enumeration phases) | Fresh-VM first-run test |
| QML effects/backdrop (Qt5 imports) | UI/polish phase | Spike MultiEffect blur early; perf check at 4K |
| Wrong launch API (CreateProcess/UAC) | App enumeration phase (launch slice) | UAC-required app + UWP launch tests |
| Startup latency (sync enumeration) | App enumeration phase | Perf budget: first show < 100ms from cache |

## Sources

- Qt for Windows Deployment (windeployqt, VC redist, plugins): https://doc.qt.io/qt-6/windows-deployment.html — HIGH
- windeployqt `--qmldir` requirement for QML imports: Qt Forum threads (e.g., /topic/128879) — HIGH (multiple sources, official doc corroborates)
- RegisterHotKey / UnregisterHotKey (failure semantics, F12, id ranges, MOD_NOREPEAT): https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey — HIGH
- UWP global hotkey limitations (suspension, no hooks): Raymond Chen via Stack Overflow; MS Q&A — HIGH for Win32 apps being the right path (this project is Win32)
- WS_EX_NOACTIVATE hover-activation bypass (WM_MOUSEACTIVATE): Raymond Chen, The Old New Thing, 2024-09-19 — HIGH
- SetWindowPos SWP_NOACTIVATE / HWND_TOPMOST semantics: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos — HIGH
- Non-activating overlay banner pattern (WS_EX_NOACTIVATE + SWP_NOACTIVATE + WM_WINDOWPOSCHANGING): SoundSwitch PRs #2156/#2241 and banner docs — MEDIUM (community, but matches MS docs)
- Exclusive-fullscreen detection via SHQueryUserNotificationState (QUNS_RUNNING_D3D_FULL_SCREEN): SoundSwitch banner docs — MEDIUM (API itself documented by MS)
- UWP enumeration + AUMID construction (PackageManager, AppxManifest, AppListEntry filters): PowerToys Run source `Microsoft.Plugin.Program/Programs/UWP.cs` — HIGH
- App Execution Aliases as 0-byte reparse points (IO_REPARSE_TAG_APPEXECLINK): https://www.tiraniddo.dev/2019/09/overview-of-windows-execution-aliases.html + MS docs — MEDIUM (reparse detail from blog)
- Get-StartApps / AUMID discovery: https://learn.microsoft.com/en-us/windows/configuration/store/find-aumid — HIGH
- ISearchQueryHelper / GenerateSQLFromUserQuery / locale inconsistency / catalog status enum: https://learn.microsoft.com/en-us/windows/win32/search/-search-3x-wds-qryidx-searchqueryhelper — HIGH
- Search indexer disabled/paused causes (optimizer apps, Game Mode, service state): Microsoft troubleshoot articles — HIGH
- Icon extraction: IShellItemImageFactory SIIGBF_INCACHEONLY / never on UI thread: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitemimagefactory-getimage — HIGH; SHGetFileInfo slowness/threading: MSDN "IExtractIcon: inflexible and may be slow" + Stack Overflow — MEDIUM/HIGH
- UWP icon indirect string → SHLoadIndirectString: https://stackoverflow.com/questions/37417757/extract-icon-from-uwp-application — MEDIUM (single source but matches PowerToys' manifest-logo approach)
- Qt6 default Per-Monitor V2 DPI awareness; SetProcessDpiAwarenessContext failures: https://doc.qt.io/qt-6/highdpi.html + Qt forum (Access denied) — HIGH
- DPI awareness must be set before HWND creation (manifest recommended): https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process — HIGH
- Qt LGPL obligations (dynamic linking, source offer, notices): https://www.qt.io/development/open-source-lgpl-obligations — HIGH
- QtGraphicalEffects → QtQuick.Effects MultiEffect: https://www.qt.io/blog/a-short-guide-to-qt-quick-effects + MultiEffect docs — HIGH
- Run key quoting/parsing ("Program" entry): Raymond Chen, The Old New Thing 2021-02-23; Gopeed issue #1338; MS Run/RunOnce docs — HIGH
- ShellExecute vs CreateProcess for elevation (ERROR_ELEVATION_REQUIRED): MS "User Account Control for Game Developers" — HIGH
- PowerToys Run program-plugin startup cost (~2.7s): PowerToys issue #18888 — HIGH (measured)
- UWP command-line activation rules (first alias wins, etc.): https://blogs.windows.com/windowsdeveloper/2017/07/05/command-line-activation-universal-windows-apps/ — HIGH

---
*Pitfalls research for: Windows application launcher (Qt6 + QML)*
*Researched: 2026-08-09*
