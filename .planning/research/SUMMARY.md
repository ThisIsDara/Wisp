# Project Research Summary

**Project:** Rofi-Windows
**Domain:** Windows application launcher (Rofi-style, Qt6 + QML)
**Researched:** 2026-08-09
**Confidence:** HIGH (with MEDIUM pockets flagged per area)

## Executive Summary

Rofi-Windows is a single-purpose Windows application launcher: a global hotkey opens a small, centered, frameless widget; typing fuzzy-matches installed apps and indexed files; Enter launches; Escape/launch/click-away dismisses. Experts (PowerToys Run, Launchy, Flow Launcher) all converge on the same skeleton — **one always-running process hosting an event-driven search pipeline behind a hidden overlay window**. The domain work is Win32/COM, not Qt: global hotkeys, Start Menu + UWP enumeration, Windows Search index queries, shell icon extraction, and DWM backdrop blur all have **no first-class Qt API**. Qt provides only the event-loop bridge and the rendering surface. PowerToys' own measurements drive the one architectural rule that matters: file-index queries are ~5x slower than everything else (~250ms vs ~50ms), so search must be split into a fast in-memory tier (apps, every keystroke) and a delayed worker-thread tier (files, debounced ~120–150ms) — never run blocking COM on the GUI thread.

The recommended stack is thin and deliberately boring: **Qt 6.11.1 open-source (NOT 6.8 LTS — its patch releases are commercial-only, leaving LGPL users stuck with known CVEs), MSVC 2022 + CMake/Ninja, C++/WinRT for UWP enumeration**, plus ~6 well-established Win32/COM entry points (`RegisterHotKey`, `IShellLink`, `PackageManager`, `ISearchQueryHelper` + OLE DB, `IShellItemImageFactory`, `ShellExecuteEx`, `DwmSetWindowAttribute`). Fuzzy matching is written in-house (~150 lines, fzf-style — every serious launcher ships its own). The window is `Qt.Tool | FramelessWindowHint` (never `Qt::Popup` — popups swallow the dismissal click and break IME). A 6-phase roadmap isolates the two riskiest components — **window focus/hotkey behavior** and **Windows Search COM** — into dedicated phases with explicit validation criteria, holds a hard **first-show < 100ms** budget, and defers nothing that is painful to retrofit (DPI defaults, LGPL decisions, the deploy script all land in Phase 1).

The top risks, all with known mitigations: (1) **hotkey/focus behavior** — silent `RegisterHotKey` failure kills the product (surface conflicts, `MOD_NOREPEAT`, unregister on quit), fullscreen games must never minimize (activation only on show, `SWP_NOACTIVATE` for z-order, `SHQueryUserNotificationState` guard), and first-show typing must work with zero clicks (ordered `show→raise→requestActivate` + `forceActiveFocus`, deferred — never enumerate inside the `WM_HOTKEY` handler); (2) **Windows Search COM** — stale/empty/blocking results (fresh `ISearchQueryHelper` per query, worker threads with per-thread COM init, catalog-status check, distinct "index still building" UX); (3) **LGPL + deployment** — decided at skeleton phase (dynamic linking only, notices scaffold) and verified at release on clean Win10/11 VMs (`windeployqt --qmldir`, official `vc_redist`, never dev-machine DLLs). Open questions (OLE DB row consumption in C++, backdrop driver quirks) are small, pre-identified spikes — nothing blocks roadmap creation.

## Key Findings

### Recommended Stack

Qt 6.11.1 + MSVC 2022 + CMake/Ninja + C++/WinRT, with Win32/COM doing all domain work behind a thin `src/win/` wrapper layer. Qt 6.8 LTS is a trap (commercial-only patches ≥6.8.4); Qt 6.12 will be the last Win10-supporting version — relevant if the Win10 support horizon extends past ~2027. Dynamic linking only (LGPL). No fuzzy-search library, no DB, no QHotkey dependency — all YAGNI at this scale (sub-100ms enumeration; raw `RegisterHotKey` + a small keycode mapper).

**Core technologies:**
- **Qt 6.11.1 (LGPLv3, dynamic)** — QML UI holds 60fps trivially; 6.11 is newest open-source line with support to 2027-03-17; do NOT use 6.8 LTS (commercial-only patches)
- **MSVC 2022 + CMake ≥3.25 + Ninja** — matches official Qt binary ABI; `qt_add_executable` + `qt_add_qml_module` are CMake-native
- **C++/WinRT (NuGet 2.x)** — documented header-only way to call `PackageManager` for UWP enumeration; COM-marshalling alternative is error-prone boilerplate
- **`RegisterHotKey` + `QAbstractNativeEventFilter`** — verified Qt6 pattern (`windows_dispatcher_MSG`); never `QWidget::nativeEvent` (silently fails in Qt 6)
- **`IShellLinkW`/`IPersistFile`** — Start Menu `.lnk` resolution (`FOLDERID_Programs` + `FOLDERID_CommonPrograms`)
- **`ISearchQueryHelper::GenerateSQLFromUserQuery` + OLE DB** — the documented Windows Search path; SQL must be generated, never hand-built; run on worker threads
- **`IShellItemImageFactory::GetImage`** — icons at any size, dpr-crisp; never on UI thread without `SIIGBF_INCACHEONLY`; `SHGetFileInfo` fallback only
- **`DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)`** — Win11 22H2+ acrylic; Win10 fallback `SetWindowCompositionAttribute` (documented-but-discouraged) or solid dark; design for graceful degradation
- **`ShellExecuteEx`** — all launches (`open` verb; `runas` only on explicit user request); `QProcess` cannot elevate; UWP via `IApplicationActivationManager` or `shell:AppsFolder\AUMID`
- **QSettings + HKCU Run key** — settings and autostart; per-user, no admin
- **windeployqt `--qmldir` + NSIS + `VC_redist.x64.exe`** — packaging; MSIX deferred to post-v1

### Expected Features

The category consensus across 8 competitors (Rofi, PowerToys Run, Launchy, Albert, Wox, Ulauncher, Listary, Flow Launcher): the core loop — hotkey → type → ↑/↓ → Enter → dismiss — is table stakes; differentiation is ranking quality, polish, and (deliberately absent here) extensibility.

**Must have (table stakes):**
- Global hotkey (default Alt+Space), configurable, with conflict surfacing (never silent failure)
- Instant fuzzy app search (Start Menu + UWP), keyboard-first nav (↑/↓/Enter/Esc/PageUp/PageDown/Home/End)
- Recency/frequency ranking + recent apps on empty query (requires stable result identity designed day one)
- File search via Windows Search index; Enter opens with default app; Ctrl+Enter opens containing folder
- Run-as-admin (Ctrl+Shift+Enter, apps only — UWP cannot elevate)
- Dismiss on Esc / launch / click-away; tray icon (Open/Settings/Quit); autostart toggle; settings (hotkey capture + accent)

**Should have (competitive):**
- Scale+fade open/close animation @60fps — the project's stated core value; competitors animate minimally or not at all
- Sleek dark theme + accent — Listary **sells** dark mode in Pro; free polished dark is a real market contrast
- Small native binary (~10–30MB vs 100MB+ web-runtime launchers)
- v1.x: fuzzy match highlighting (matcher returns positions), Alt+number quick-select, game mode

**Defer (v2+):**
- Plugin SDK, clipboard history, window switching, web search, AI/MCP, system commands, emoji picker (Windows has Win+V / Win+. / Win+Tab built-ins), custom directory indexing (Windows Search exists), Everything integration (keep backend behind a thin interface so it can slot in later)

### Architecture Approach

Single process; `QAbstractListModel` + QML ListView; worker threads (`QtConcurrent`/`QThreadPool`, `CoInitializeEx` per thread, COM objects never shared across threads) for catalog build, file queries, and icons — all model updates marshaled via queued signals. Window is `Qt.Tool | FramelessWindowHint` (Launchy's proven pattern — NOT `Qt::Popup`). Two-tier search: tier-1 in-memory app match (<5ms, every keystroke), tier-2 Windows Search (after 120–150ms debounce, results merged/inserted incrementally). Generation counter drops stale results. Score = fuzzy (0–100) + sourceBoost(apps over files) + usageBoost. `src/win/` is a firewall — all Win32/COM behind Qt-friendly wrappers, keeping `src/core` unit-testable.

**Major components:**
1. **HotkeyService** — `RegisterHotKey` + native event filter → `hotkeyPressed()` signal; unregister on quit
2. **LauncherController** — show/hide/toggle, centering, focus sequencing, launch dispatch; owns the "show→raise→requestActivate" order
3. **AppIndex** — async Start Menu `.lnk` + UWP (`PackageManager`) catalog; filtered (drop frameworks, `AppListEntry=none`, empties); deduped against .lnk
4. **FileSearchService** — fresh `ISearchQueryHelper` per query + OLE DB on pool thread; catalog-status awareness
5. **ResultModel** — `QAbstractListModel` (roles: Title, Subtitle, Icon, Score, stable id); batch resets per keystroke, incremental insert for tier-2
6. **IconService** — async extraction + QCache/disk cache; `SIIGBF_INCACHEONLY` fast path; UWP indirect-string resolution (`SHLoadIndirectString`)
7. **WinLaunch** — single action-runner module for all launches (elevation guard lives in one place)
8. **Tray/Settings/Autostart** — `QSystemTrayIcon` (C++ — QML has no tray), QSettings, quoted HKCU Run key, `QLocalServer` single-instance

### Critical Pitfalls

1. **Hotkey silently fails to register** — `ERROR_HOTKEY_ALREADY_REGISTERED` (or F12, kernel-reserved) leaves the product dead with no error. Check return + `GetLastError()`, surface a tray notification with a settings path, `MOD_NOREPEAT`, validate combos, unregister on exit. Test against another launcher owning Alt+Space.
2. **Focus theft from fullscreen games / no keyboard on first show** — activating the window minimizes exclusive-fullscreen games (the #1 "obnoxious launcher" complaint); meanwhile `requestActivate()` is a *request* Windows can deny, so first-show typing fails. Activate deliberately on show (the user asked via hotkey), never re-activate after; `SWP_NOACTIVATE` for z-order; `WM_MOUSEACTIVATE` → `MA_NOACTIVATE`; `SHQueryUserNotificationState` fullscreen guard. Acceptance test: type immediately after hotkey, first time, zero clicks; game must not minimize.
3. **UWP enumeration junk/misses** — WindowsApps dir is ACL-locked; `PackageManager` returns frameworks and `AppListEntry=="none"` stubs; App Execution Aliases are 0-byte reparse points that can't `CreateProcess`. Use PackageManager + manifest `<Application>` parsing, PowerToys' filter set, AUMID launch, dedupe vs .lnk. Golden-list tests (Calculator/Terminal/Notepad present, no junk).
4. **Windows Search: stale, empty, or blocking** — hand-built SQL breaks; cached `ISearchQueryHelper` goes stale; indexer disabled/building returns empty with no error; OLE DB on UI thread freezes. Fresh helper per query via `GenerateSQLFromUserQuery`, `GetCatalogStatus` check with distinct "index still building/disabled" UX (never dead-air "no results"), worker threads, `put_QueryMaxResults`, debounce.
5. **LGPL + deployment discovered at release = blocked release** — static Qt linking forces GPL obligations; `windeployqt` without `--qmldir` omits QML modules (the most common Qt6 Windows failure); dev-machine VC DLLs are unlicensed to redistribute. Decide dynamic-linking + notices at skeleton phase; standardize the deploy command; clean-VM (Win10 + Win11, no dev tools) smoke test as release gate; official `vc_redist`.

## Implications for Roadmap

Suggested structure — 6 phases, each leaving a runnable, demoable app. The two riskiest components (window focus behavior, Windows Search COM) get isolated phases with explicit validation criteria; nothing hard-to-retrofit (DPI, LGPL, deploy script, result identity) is deferred. Performance contract throughout: **first show < 100ms, animations 60fps, file results never block typing**.

### Phase 1: Core Shell — Qt scaffold, window, animation, compliance seed
**Rationale:** Everything depends on the runnable shell; window flags are a known quagmire (`Qt::Popup` vs `Qt.Tool|Frameless`) and get settled first; DPI defaults and LGPL decisions are retrofit-proof — done now or painful later.
**Delivers:** CMake/Qt6 Quick scaffold; frameless tool window, centered, `Qt.Tool | FramelessWindowHint`; scale+fade 150–200ms @60fps open/close; DPI correctness (Qt owns PMv2 — never call `SetProcessDpiAwareness*`); LGPL scaffold (dynamic-linking lock-in, `THIRD-PARTY-NOTICES.txt` stub); `windeployqt --qmldir` deploy script seed.
**Addresses:** FEATURES — animation core value, dark-theme seed.
**Avoids:** PITFALLS — #7 (DPI), #8 (LGPL deferred), #9 (deploy gaps found at release), Qt::Popup trap.
**Research:** standard Qt patterns — **skip research-phase**.

### Phase 2: Hotkey + Show/Hide + Focus Behavior
**Rationale:** The "hotkey → type → Enter" muscle memory is the product's identity; this is risky-component #1 and gets its own phase with hard acceptance criteria (PITFALLS 1–3 all concentrate here).
**Delivers:** `RegisterHotKey` + `QAbstractNativeEventFilter` (`windows_dispatcher_MSG`); toggle logic; conflict surfacing (tray notification + settings path); ordered focus sequence (`show→raise→requestActivate` deferred, `forceActiveFocus` on visible/active change); fullscreen guard (`SHQueryUserNotificationState`); dismissal (Esc + focus-loss with ~100–200ms grace timer); minimal QSettings store so the hotkey is configurable from day one.
**Addresses:** FEATURES — configurable hotkey, keyboard-first, dismiss behaviors (P1).
**Avoids:** PITFALLS — #1 (silent hotkey failure), #2 (game focus theft), #3 (no first-show typing).
**Research flag:** focus-suppression pattern (`WS_EX_NOACTIVATE`/`SWP_NOACTIVATE`/`WM_MOUSEACTIVATE`, QUNS detection) rests on community sources (SoundSwitch) — **validate in a spike** with a real exclusive-fullscreen game. Also decide the click-outside dismissal mechanism explicitly (deactivation-based, per ARCHITECTURE — conflicts with the no-activate pattern).

### Phase 3: Result Model + App Catalog (first vertical slice)
**Rationale:** Tier-1 search is the in-memory catalog — the core promise delivered early; stable result identity must be designed here because recency depends on it (retrofitting breaks the ranking store); launch logic ships with results (PITFALLS 13/14 live here).
**Delivers:** `ResultModel` (`QAbstractListModel`, roles incl. stable id: exe path / AUMID / file path); AppIndex (`.lnk` via `IShellLink` + UWP via `PackageManager`/C++/WinRT, PowerToys-style filters, deduped); in-house fzf-style fuzzy ranker (unit-tested, returns match positions for later highlighting); generation counter + debounce scaffold; launch via `ShellExecuteEx`/`IApplicationActivationManager`; run-as-admin (Ctrl+Shift+Enter, UWP guarded) in one action-runner module; usage recording + empty-query recent list; keyboard nav + Enter-freeze-selection.
**Addresses:** FEATURES — fuzzy app search, recency, run-as-admin, launch, keyboard nav (all P1).
**Avoids:** PITFALLS — #4 (UWP junk), #13 (CreateProcess/elevation), #14 (startup latency; budget: cache build off the hotkey path, first show < 100ms).
**Research flag:** feasibility check `Get-StartApps` vs `IShellFolder` .lnk enumeration (PITFALLS sources cite both) — small spike; PowerToys `UWP.cs` is the reference for filters otherwise.

### Phase 4: File Search (Windows Search COM)
**Rationale:** Risky-component #2 — the only 200–500ms operation in the product. It lands only after the pipeline (debounce, generation counter, model merge) is proven in Phase 3; PowerToys' delayed-execution design is the direct precedent.
**Delivers:** `FileSearchService` — fresh `ISearchQueryHelper` per query (`GenerateSQLFromUserQuery`, `put_QueryWhereRestrictions`, `put_QueryMaxResults(30)`) + OLE DB on pool thread (`CoInitializeEx` per thread, per-thread serialization); tier-2 incremental merge into the model; `GetCatalogStatus` + degraded-state UX ("index still building" / "search disabled" — distinct, friendly states, never blank); open-with-default-app; Ctrl+Enter open containing folder (`explorer /select`).
**Addresses:** FEATURES — file search (P1), open containing folder (P1).
**Avoids:** PITFALLS — #5 (stale/empty/blocking search), #11 (first-launch empty experience).
**Research flag:** **needs spike** — OLE DB row consumption in C++ (ATL `CDataSource`/`CCommand` vs ADO); MS's DSearch sample is C#.

### Phase 5: Polish — Icons, Backdrop, Theme, Ranking
**Rationale:** Polish layers depend on Phase 3 artifacts (icon role, stable identity); the two finicky subsystems (backdrop blur, UWP icons) each get a spike before full wiring.
**Delivers:** async `IconService` (`IShellItemImageFactory` off UI thread, `SIIGBF_INCACHEONLY` fast path, QCache + disk cache, dpr-aware sizes, UWP indirect-string → `SHLoadIndirectString` + `scale-*` variant selection); DWM acrylic (Win11 22H2+) with Win10 `SetWindowCompositionAttribute` fallback and solid-dark escape hatch; accent theming using `QtQuick.Effects` `MultiEffect` (never Qt5Compat `QtGraphicalEffects`); ranking refinement (usage-boost tuning, optional match highlighting).
**Addresses:** FEATURES — differentiators: dark theme + accent (P1), v1.x highlight.
**Avoids:** PITFALLS — #6 (icon blocking/UWP icons), #12 (Qt5 effects muscle memory, blur perf).
**Research flags:** **spike** — transparent `QQuickWindow` + `DwmSetWindowAttribute` driver quirks (solid fallback is the escape hatch); **verify** — mixed-DPI `IShellItemImageFactory` behavior.

### Phase 6: Tray, Settings, Autostart + Packaging
**Rationale:** Background services depend on `SettingsService`; packaging must come last (needs a working binary) but its script has existed since Phase 1 — nothing discovered here should be a surprise.
**Delivers:** tray (`QSystemTrayIcon`, Open/Settings/Quit), single-instance (`QLocalServer`), settings UI (hotkey capture, accent, autostart toggle), autostart via quoted HKCU Run key + `--autostart` arg; NSIS per-user installer bundling `VC_redist.x64.exe` + `THIRD-PARTY-NOTICES.txt` + source offer; LGPL verification (relink test); clean-VM release gates (Win10 22H2 + Win11 24H2 smoke: install, launch, hotkey, launch app, search file); autostart round-trip test; hotkey-conflict re-test with another launcher installed.
**Addresses:** FEATURES — tray+autostart (P1), settings (P1), installer release gate (P1).
**Avoids:** PITFALLS — #8 (LGPL final verification), #9 (deploy gaps), #10 (autostart registry mistakes).
**Research:** standard patterns (QSettings/registry, QSystemTrayIcon, NSIS) — **skip research-phase**; execution discipline is the risk, not knowledge.

### Phase Ordering Rationale
- **Risky components isolated:** window focus behavior (P2) and Windows Search COM (P4) each get dedicated phases with validation criteria and test scenarios (fullscreen game; indexer-disabled machine) — they never share a phase with other unknowns.
- **Retrofit-proof items early:** DPI defaults + LGPL decision + deploy script in P1; stable result identity in P3. Each costs minutes now and a refactor later.
- **Dependency-driven grouping:** shell → hotkey → model+catalog → file search → polish → background services/packaging; every phase leaves a runnable app (ARCHITECTURE's build order validated).
- **Performance contract embedded per phase:** first show < 100ms (P3), 60fps animation (P1), debounce + generation counter before COM queries exist (P3), lazy icons (P5).
- **Clean-VM + LGPL verified at release (P6) but prepared since P1** — the "looks done but isn't" checklist (PITFALLS) is the release gate.

### Research Flags
Phases likely needing deeper research during planning:
- **Phase 4:** OLE DB row consumption in C++ — MS sample is C#; ATL vs ADO decision — spike
- **Phase 5:** transparent QQuickWindow + DWM backdrop driver quirks — spike with solid fallback; mixed-DPI `IShellItemImageFactory` behavior — verify
- **Phase 2:** fullscreen-guard + no-activate pattern — validate against real exclusive-fullscreen app
- **Phase 3:** `.lnk` enumeration method (Get-StartApps vs IShellFolder) — small feasibility check

Phases with standard patterns (skip research-phase):
- **Phase 1:** Qt QML window + animation — thoroughly documented
- **Phase 6:** QSettings, tray, NSIS, HKCU Run — all well-established

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All critical API names/patterns verified against official Qt 6.11 + Microsoft Learn docs on 2026-08-09; MEDIUM on C++/WinRT URL (moved), ShellExecuteEx/IShellLink/Run-key (well-established, not re-fetched) |
| Features | HIGH | Competitor landscape verified against official docs (Rofi, PowerToys, Flow, Listary, Ulauncher, Wox); Flow Alt+number third-party-cited only (MEDIUM), new-Wox/Raycast-for-Windows (LOW, not central) |
| Architecture | HIGH | PowerToys Run source + measured perf data (250ms index queries, 2.7s plugin init, icon ~20% query time), Launchy source as working reference, Qt/MS docs; QHotkey and a few forum threads MEDIUM |
| Pitfalls | HIGH | Mostly official docs + PowerToys source + Raymond Chen; MEDIUM on SoundSwitch-derived focus-suppression pattern and reparse-point blog |

**Overall confidence: HIGH** — sufficient for roadmap creation; MEDIUM pockets map 1:1 to the research flags above.

### Gaps to Address
- **OLE DB C++ consumption pattern**: MS's DSearch sample is C#; ATL `CDataSource`/`CCommand` vs ADO unresolved — spike in Phase 4 planning
- **Settings storage location — research conflict**: STACK.md recommends registry (NativeFormat, platform convention, co-locates autostart); ARCHITECTURE.md recommends `%APPDATA%` INI. Either works; resolve during Phase 2 planning (INI is more testable/portable; registry is more conventional)
- **Click-outside dismissal vs WS_EX_NOACTIVATE lifecycle**: explicitly flagged by PITFALLS as a decision (deactivation-based dismissal with grace timer is the recommended path since this window needs keyboard focus) — settle in Phase 2 spike
- **Fullscreen-guard UX decision**: defer the popup while a game is exclusive-fullscreen vs show anyway — product decision for Phase 2
- **Icon cache keying/eviction**: key = app id + size + dpr variant; LRU bounds to avoid the documented leak pattern — design in Phase 5
- **Recency store format**: FEATURES says small JSON store; trivial, resolve in Phase 3
- **Win10 support horizon**: Qt 6.12 is the last Win10-supporting Qt line — if Win10 matters past ~2027, pin ≤6.12 when the time comes

## Sources

### Primary (HIGH confidence)
- Qt official docs (6.11): `QAbstractNativeEventFilter` (hotkey path), windows-deployment (`windeployqt --qmldir`, VC redist licensing), qt-releases (6.8 LTS commercial-only), windows.html (Win10 EOL), highdpi (PMv2), `QSettings`, QML Window/Popup behavior
- Microsoft Learn: `RegisterHotKey` (failure semantics, F12, MOD_NOREPEAT), `ISearchQueryHelper`/catalog status, `IShellItemImageFactory::GetImage`, `DwmSetWindowAttribute`/`DWMSBT_TRANSIENTWINDOW`, `PackageManager.FindPackagesForUser`, `IApplicationActivationManager`, `ShellExecuteEx` "runas", Run/RunOnce keys, `SetWindowCompositionAttribute`, `SHQueryUserNotificationState`, high-DPI awareness, app-execution aliases
- PowerToys Run source + docs: architecture doc (fast/delayed split), `UWP.cs` (enumeration filters), `StringMatcher.cs` (scoring), `Result.cs` (GetSortOrderScore), issue #7456 (icon perf), issue #18888 (2.7s init)
- Launchy source (`LaunchyWidget.cpp`): window flags, hide-on-focus-lost, SetForegroundWindow workaround — working production reference
- Rofi (README/manpage), Flow Launcher (README/hotkey table), Listary docs, Ulauncher, Wox — competitor feature facts
- fzf source (`algo.go`, FuzzyMatchV2) — matching algorithm
- qt.io LGPL obligations page; Raymond Chen (Old New Thing: hover-activation, icon overlay, Run-key quoting)

### Secondary (MEDIUM confidence)
- SoundSwitch PRs #2156/#2241 + banner docs — non-activating overlay pattern, fullscreen detection
- Qt forums + Stack Overflow — QSettings consensus, Qt::Popup dismissal quirks, Qt6 nativeEvent failure reports, UWP icon indirect strings (`SHLoadIndirectString`), PackageManager→AUMID enumeration
- NSIS vs WiX comparisons; Windows-classic-samples DSearch (C# reference); selastingeorge/Win32-Acrylic-Effect (blur lag history)

### Tertiary (LOW confidence)
- tiraniddo.dev overview of App Execution Aliases (reparse-point detail — matches MS docs, blog primary)
- WindowsForum/MakeUseOf reviews of Flow/Listary (Alt+number corroboration only)
- New-Wox (Go/Flutter) and Raycast-for-Windows specifics — not central, brief exposure

---
*Research completed: 2026-08-09*
*Ready for roadmap: yes*
