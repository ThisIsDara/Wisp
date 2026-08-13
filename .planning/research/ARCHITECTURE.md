# Architecture Research

**Domain:** Windows application launcher (Qt6 + QML)
**Researched:** 2026-08-09
**Confidence:** HIGH (patterns verified against PowerToys Run source, Launchy source, Microsoft Learn, Qt 6 docs; LOW/MEDIUM flags noted inline)

## Standard Architecture

Launchers (Rofi, PowerToys Run, Launchy, Flow Launcher, Wox) share the same skeleton: a **single always-running process** hosting an **event-driven search pipeline** behind a **borderless overlay window**. The window is normally hidden; a global hotkey shows it with focus on the text field; typing produces ranked results; Enter launches; Escape / focus loss / click-away dismisses. Everything else (tray, settings, catalog building) is background plumbing.

PowerToys Run's own docs confirm the canonical split: UI layer, a results/query controller (their MVVM ViewModel), and pluggable search "providers" each returning scored `Result`s which the controller merges and sorts by score. Their key performance lesson: **file-index queries are ~5x slower than every other provider combined (~250ms vs ~50ms)**, so they split providers into "fast" (run on every keystroke) and "delayed" (file search runs after fast results are already painted).

### System Overview

```
                     ┌─────────────────────────────────────────────────┐
                     │              PROCESS (single instance)          │
                     │                                                 │
  WM_HOTKEY ───────▶ │  HotkeyService ── signal ──▶ LauncherController │
                     │                                  │  ▲           │
                     │                                  ▼  │           │
                     │  ┌──────────────────────────────────┐ │           │
                     │  │  QML Window (Qt.Tool|Frameless)  │ │           │
                     │  │  TextField ◀── bind ──▶ model     │ │           │
                     │  └──────────────────────────────────┘ │           │
                     │        │ textChanged (debounced)      │           │
                     │        ▼                              │           │
                     │  SearchService (QThreadPool)          │           │
                     │   ├─ AppIndex (in-memory catalog)     │           │
                     │   ├─ FileSearch (Windows Search COM)  │           │
                     │   └─ IconLoader (async)               │           │
                     │        │ queued results               │           │
                     │        ▼                              │           │
                     │  ResultModel (QAbstractListModel)     │           │
                     │        │                              │           │
                     │  SettingsService ◀── tray/UI ──▶ TrayService     │
                     │  AutostartService (registry Run key)             │
                     └─────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Typical Implementation |
|---|---|---|
| **HotkeyService** | Registers/unregisters global hotkey (default Alt+Space), converts `WM_HOTKEY` into a Qt signal | `QAbstractNativeEventFilter` + `RegisterHotKey()`; or the mature `QHotkey` library (Skycoder42, MIT). Emits `hotkeyPressed()`. **Qt6 gotcha:** filter must use `qintptr* result` signature, and the event filter approach works where `QWidget::nativeEvent` silently fails in Qt 6.x |
| **LauncherController** | Owns show/hide/toggle logic, window centering, input focus, request sequencing, launch dispatch | QObject singleton exposed to QML; decides "am I visible? → hide : show" |
| **AppIndex** | Builds the in-memory catalog of installed apps at startup (Start Menu `.lnk` + UWP); refresh on demand | Background thread (QThread). Start Menu: enumerate `%ProgramData%\Microsoft\Windows\Start Menu\Programs` + `%APPDATA%\...\Start Menu\Programs`, resolve `.lnk` via COM `IShellLink`/`IPersistFile`. UWP: `PackageManager::FindPackagesForUserWithPackageTypes` + `GetPackageApplicationIds()` → AUMID (for `IApplicationActivationManager` launch). Store {name, keywords, path/AUMID, source} |
| **FileSearchService** | Queries the Windows Search index for files | COM `ISearchQueryHelper` (build SQL from AQS) + OLE DB (`provider=Search.CollatorDSO`) executed via ATL `CDataSource`/`CCommand` on a pool thread. Must `CoInitializeEx` per worker thread |
| **ResultModel** | `QAbstractListModel` exposing title/subtitle/icon/score to QML ListView; batch updates | Lives on GUI thread only; filled via queued signal from search threads. Roles: `Title`, `Subtitle`, `Icon`, `Score`, `HighlightedRanges` |
| **IconService** | Async icon extraction with in-memory + disk cache | Pool thread; `SHGetImageList`/`SHGetFileInfo(SHGFI_SYSICONINDEX)` for the system image list index, `IShellItemImageFactory::GetImage` for thumbnails — **never on the GUI thread** (MSDN explicit). Convert `HBITMAP`/`HICON` → `QPixmap` on GUI thread |
| **SettingsService** | Loads/saves hotkey, accent color, theme, always-on-top, autostart flag | `QSettings` with `IniFormat` in `%APPDATA%` (see Settings storage section) |
| **TrayService** | System tray icon, menu (Show, Settings, Quit), single-instance signal to existing process | `QSystemTrayIcon` + `QMenu`; `QLocalServer`/`QSharedMemory` for single-instance |
| **AutostartService** | Enables/disables launch-at-login | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` value pointing at exe (no elevation needed); alternative: Startup-folder `.lnk` |
| **LauncherService** (merged into Controller) | Executes a result: `.lnk`/`.exe` via `ShellExecuteEx`, UWP via `IApplicationActivationManager::ActivateApplication`, file via `ShellExecuteEx` default verb | Windows API calls on GUI thread (fast, non-blocking) — then hide window |

### Process Model — Single Process, Threaded Workers

**Single process.** No separate helper process, no out-of-process search server. PowerToys Run, Launchy, Flow Launcher all do it in one process; Windows Search and the shell do the heavy lifting for us.

**Threading rules (the whole architecture hinges on these):**
1. **GUI thread** — owns QML window, ResultModel mutations, launch calls. Nothing slow ever runs here. The 60fps animation requirement means the render thread must never stall.
2. **Worker threads** — search queries, catalog building, icon extraction. Use `QThreadPool` + `QtConcurrent::run` (or a small number of `QThread`s with queued results). All COM objects used on workers must be created and used on the same worker (`CoInitializeEx` per thread) — do **not** share COM interfaces across threads.
3. **Model updates are marshaled** from worker → GUI via signal/slot **queued connections** (or `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`). The model is never touched from a worker.

Why not run search inline? PowerToys measurements: a Windows-index query takes ~200–500ms on cold machines (`GenerateSQLFromUserQuery` alone ≈ 200ms). Blocking the UI thread for that breaks both the open-to-ready latency and the animation budget. This is the single most common launcher architecture mistake.

---

## Recommended Project Structure

CMake-based Qt6 layout (Qt's own convention for Qt Quick apps):

```
Rofi-Windows/
├── CMakeLists.txt                  # qt_standard_project_setup(), find_package(Qt6 ...)
├── src/
│   ├── app/                        # main.cpp, Application (wiring, single-instance)
│   ├── core/                       # C++ services (no QML deps)
│   │   ├── hotkey/HotkeyService.*
│   │   ├── search/AppIndex.*, FileSearchService.*, SearchService.*
│   │   ├── model/ResultModel.*, ResultItem.*, ScoreRanker.*
│   │   ├── icons/IconService.*
│   │   ├── settings/SettingsService.*
│   │   ├── launcher/LauncherController.*
│   │   └── tray/TrayService.*, AutostartService.*
│   └── win/                        # thin Win32/COM wrappers (pure Windows)
│       ├── WinStartMenuEnumerator.*
│       ├── WinUwpEnumerator.*
│       ├── WinSearchQuery.*
│       ├── WinIconLoader.*
│       └── WinLaunch.*
├── qml/                            # QML UI (imports RofiWindows 1.0 for C++ types)
│   ├── MainWindow.qml              # the launcher window
│   ├── ResultList.qml, ResultDelegate.qml, SearchField.qml
│   └── theme/                      # colors, accent, blur/transparency
├── tests/                          # Qt Test: ranker, enumerators (mocked), model
└── packaging/                      # installer scripts (Inno Setup) + LGPL notices
```

### Structure Rationale

- **`src/win/` is a firewall**: all Win32/COM calls live behind thin wrappers with Qt-friendly signatures (e.g., `QVector<AppEntry> WinStartMenuEnumerator::scan()`). Services depend on these interfaces, not on `windows.h`. This isolates the platform-specific code so it can be reviewed/tested in isolation and keeps `src/core` unit-testable without a real Windows shell. (PowerToys/Launchy both isolate platform code similarly.)
- **`src/core` has no QML dependency**: services emit plain Qt signals; only `ResultModel` and `LauncherController` are `Q_INVOKABLE`/context-property types. This keeps the model testable and the UI swappable.
- **`qml/` is pure presentation**: window flags, animation, and layout live in QML; all behavior is in C++. No logic in `onTextChanged` handlers beyond calling into the controller.
- Type registration: one static registration function (`qmlRegisterType` / `QML_ELEMENT`) called from `main.cpp`; C++ types are context properties (`engine.rootContext()->setContextProperty("launcher", ...)`) for singletons like the controller and settings.

---

## Architectural Patterns

### 1. Observer / signal-slot (Qt-native)
Every boundary is a signal: `HotkeyService::hotkeyPressed`, `SearchService::resultsReady(requestId, results)`, `IconService::iconReady(key, pixmap)`, `SettingsService::hotkeyChanged`. Workers never call into GUI code directly — they emit; queued connections do the marshaling. This is the pattern that makes the threading rules enforceable.

### 2. Model-View (QAbstractListModel ↔ QML ListView)
`ResultModel` is the classic Qt model-view contract: roles, `rowCount`, `data`. Update strategy matters for feel:
- **Batch replaces** with `beginResetModel()/endResetModel()` for each keystroke round-trip (dozens of rows, reset is cheap and avoids per-row churn).
- Or incremental `beginInsertRows`/`dataChanged` when merging delayed file results into already-painted app results (matches PowerToys' "fast results first, files arrive later" flow).
- Keep `Listview` `cacheBuffer` tuned and delegate creation light; icons arrive asynchronously via `IconService` (see below) and update via `dataChanged`.

### 3. Service layer / dependency injection by constructor
Each service takes its dependencies explicitly (`SearchService(AppIndex*, FileSearchService*)`), enabling test doubles. Settings injected into services that need them (hotkey into HotkeyService, always-on-top into controller). No service globals; the `Application` object owns composition and lifetime.

### 4. Generation counter for request cancellation (search pipeline)
Every search gets a monotonically increasing request id; the model only applies results whose id is still current. This is how stale keystroke results are dropped — simpler and more robust than thread cancellation for a COM query mid-flight. PowerToys uses the equivalent (`CancellationToken`).

### 5. Debounce + two-tier search
TextField changes → controller restarts a `QTimer` (singleShot, ~120–150ms) → on timeout, fire search. PowerToys' finding that indexer queries are the slow tier argues for: **tier 1** = app index match (in-memory, <5ms, every keystroke); **tier 2** = Windows Search file query (200–500ms, after debounce, results appended/merged). Never run tier 2 on every keystroke.

---

## Data Flow

### Key Data Flows

**Show/hide flow:**
```
RegisterHotKey → WM_HOTKEY → HotkeyService filter → signal
→ LauncherController::toggle()
  if hidden:  set query text empty → position window centered on primary screen
              → window.visible = true → scale/fade animation (~150-200ms, 60fps)
              → window.requestActivate() → TextField.forceActiveFocus()
  if visible: fade out → visible = false
```
Focus note: after showing from a hotkey, Windows grants the foreground right to the process that owns the hotkey — `requestActivate()` works (Launchy still needs a `SetForegroundWindow` workaround in edge cases; Qt 6's `Window::requestActivate()` handles the common path).

**Search flow:**
```
TextField.onTextChanged
  → LauncherController::onQueryChanged(text)
  → debounceTimer.restart(120ms)
  → SearchService::search(query, ++requestId)      [GUI thread call]
      → QtConcurrent::run(pool):
          tier1: AppIndex.fuzzyMatch(query)  → scored AppResults      (~<5ms, in-memory)
          tier2: if query.length ≥ 2: FileSearchService.query(query)  (200-500ms, COM)
                 → SQL via ISearchQueryHelper::GenerateSQLFromUserQuery
                 → OLE DB execute on THIS thread (CoInitializeEx'd)
      → emit resultsReady(requestId, mergedSortedResults)
  → (queued) ResultModel::replaceResults(requestId, results)
      if requestId != current: drop (stale)
      else: beginResetModel / merge tier2 → endResetModel; emit dataChanged for icons
  → QML ListView repaints; IconService requested per row (lazy)
```

**Launch flow:**
```
ListView Enter/click → LauncherController::launch(selected ResultItem)
  → WinLaunch::launch(result)   [GUI thread, non-blocking]
      .lnk/.exe  → ShellExecuteEx(verb=open)         (honors UWP-bridge .lnk too)
      UWP AUMID  → IApplicationActivationManager::ActivateApplication(aumid, AO_NONE)
      file       → ShellExecuteEx(verb=open) (default handler)
  → hide window; record usage (for usage-based ranking); SettingsService::sync()
```

**Ranking / merge (apps vs files):**
- Each source produces results with a **fuzzy-match score** in a common 0–100 scale. PowerToys' `StringMatcher` scoring rules are the proven model: bonus for match at word start / near string start, bonus for contiguous matches, penalty for spread-out matches; substring-match beats character-match.
- **Source boost** so the merge feels right: apps get a flat boost over files (e.g., apps rank above files unless the file match is exact — PowerToys gives plugins `WeightBoost`). Exact-name app match >> file match; partial app match ≈ partial file match with slight app preference.
- **Usage boost** (optional, default on): `finalScore = fuzzyScore + sourceBoost + usageCount × smallMultiplier` — PowerToys' `GetSortOrderScore` pattern (`WeightBoost + Score + SelectedCount * multiplier`). Disableable via settings.
- The merge is a single stable sort over `finalScore`, then truncate to N (e.g., 10–20 results).
- Empty query → show default list (most-used apps, then alphabetical) — this doubles as the "idle" state and hides search latency entirely on first open.

**Icon flow:**
```
Listview delegate created → role Icon returns placeholder
→ ResultModel requests IconService.icon(iconKey)
→ pool thread: SIIGBF_MEMORYONLY first → disk cache file → SIIGBF_INCACHEONLY
  → extraction only if uncached (SHGetImageList index / IShellItemImageFactory)
→ emits iconReady(key, pixmap) → model dataChanged(row) → delegate swaps in pixmap
```

---

## Scaling Considerations

Single-user desktop app — the scaling curve is about **input rate and first-run**, not concurrency:

| Concern | At 1 user, cold start | At 1 user, steady state |
|---|---|---|
| Open latency | App catalog must be built → build in background at launch, UI still opens instantly with "no results yet" → catalog ready signal; never block open on catalog | Catalog cached in memory; optionally persisted to disk (AppData) and revalidated — open <100ms |
| Keystroke throughput | Tier-1 in-memory match keeps up easily | Debounce + generation counter drops stale COM queries; pool (2–4 threads) prevents queue pile-up |
| Windows Search COM | First query initializes OLE DB/ATL objects lazily on worker (CoInitialize once per thread) | Reuse cached `ISearchQueryHelper` + connection per thread; watch `ExecuteReader` races — PowerToys hit "reader closed" exceptions under concurrent queries, fixed by serializing per-thread access |
| Icon load | Uncached extraction is the slow tier (PowerToys: ~20% of total query time) | In-memory QCache + disk cache in AppData makes it ~0; lazy 0.5s-after-results loading hides residual cost |
| Focus/window | `SetForegroundWindow` races if user is in a fullscreen app — Qt requestActivate covers normal cases | Tool window never appears in Alt+Tab/taskbar; always-on-top flag toggled by settings |

**What breaks first:** UI freeze from a blocking call on the GUI thread (search, icon extraction, or `ISearchQueryHelper::GenerateSQLFromUserQuery` called inline) — everything else degrades gracefully. The second breaker: focus fights between the launcher window and the previously-active app when using `Qt::Popup`-type windows (popup windows swallow input and have platform quirks; see Anti-Patterns).

---

## Anti-Patterns

### 1. Running search on the GUI thread
**What:** calling the Windows Search query inline in the textChanged handler.
**Why bad:** 200–500ms stalls — kills the 60fps animation and open-to-ready latency, which is the product's core value. PowerToys' whole delayed-execution design exists because of this.
**Instead:** debounce + pool thread + queued model update (Data Flow above).

### 2. `Qt::Popup` as the launcher window type
**What:** using `Qt.Popup` (or QML `Popup` with `popupType: Popup.Window`) to get free click-outside dismissal.
**Why bad:** Popup windows grab the mouse/keyboard and swallow the dismissal click (documented Windows behavior — the click that closes the popup does not reach the app behind it); they break IME input on some configurations, don't take normal focus, and their behavior shifted across Qt versions (Qt 4.8 changed `FocusOutEvent` semantics; popup type historically had to be a QDialog to dismiss reliably). For a window that hosts a **text field with IME and needs reliable keyboard focus**, Popup is the wrong tool.
**Instead:** `Qt.Tool | Qt.FramelessWindowHint` (optionally `| Qt.WindowStaysOnTopHint`) — exactly what Launchy ships on Windows — and implement dismissal via `Window.onActiveChanged`/`QEvent::WindowDeactivate` (hide when the window loses activation), with a ~100–200ms grace timer so opening the settings dialog doesn't instantly hide the launcher.

### 3. Sharing COM interfaces across threads
**What:** creating `ISearchQueryHelper`/`IShellItem` on the GUI thread and using it on a worker.
**Why bad:** COM apartment violations — intermittent hangs and crashes (PowerToys' "random crashes calling IShellItem from non-UI thread").
**Instead:** create and use every COM object on one worker thread; `CoInitializeEx(COINIT_MULTITHREADED)` per thread; marshal only plain data (QString, QVariant) across threads.

### 4. Re-scanning the app catalog on every show
**What:** enumerating Start Menu + UWP packages each time the window opens.
**Why bad:** UWP enumeration via PackageManager is slow (hundreds of packages, manifest reads); it delays every open.
**Instead:** build once at startup in background; refresh on-demand (settings action / every N hours / after a 5-minute idle scan). PowerToys/Launchy both cache catalogs.

### 5. Blocking tray/quit on shutdown
**What:** letting the process die with registered hotkeys or half-written settings.
**Why bad:** orphaned `RegisterHotKey` (until reboot), corrupt settings file.
**Instead:** `UnregisterHotKey` on quit; `QSettings::sync()`; handle `WM_ENDSESSION`/`aboutToQuit` (Launchy saves settings on `WM_ENDSESSION`).

### 6. Storing settings next to the exe
**Why bad:** if installed to `Program Files`, writes need elevation (UAC); breaks "clean install" requirement.
**Instead:** `%APPDATA%\Rofi-Windows\` (QSettings IniFormat default location), per the QSettings best-practice guidance.

---

## Integration Points

### External Services (Win32/COM APIs as services)

| API | Service | Purpose | Notes |
|---|---|---|---|
| `RegisterHotKey` / `WM_HOTKEY` | HotkeyService | Global hotkey; `MOD_NOREPEAT` to avoid auto-repeat toggling; `UnregisterHotKey` on change/quit | Alt+Space is taken as default — it's also the active window's system menu shortcut; `RegisterHotKey` wins over the system, and PowerToys Run ships Alt+Space as default, so this is proven safe (HIGH) |
| `IShellLink` + `IPersistFile` (COM) | WinStartMenuEnumerator | Resolve `.lnk` targets: display name, exe path, icon location, working dir | Scan both user and machine Start Menu dirs; dedupe by AUMID/name |
| `PackageManager::FindPackagesForUserWithPackageTypes` + `GetPackageApplicationIds` (WinRT/COM) | WinUwpEnumerator | Enumerate UWP/Store apps → AUMID per app (not per package: parse package manifest `<Application Id>` entries, AUMID = PackageFamilyName + "!" + AppId) | No admin needed for current user (empty SID); use `PackageType_Main\|Optional` filter (MEDIUM — details verified via MSDN sample + SO threads) |
| `ISearchManager`/`ISearchCatalogManager::GetQueryHelper` + OLE DB `Search.CollatorDSO` | FileSearchService | Query the Windows Search index; `GenerateSQLFromUserQuery` converts AQS→SQL; execute via ATL `CDataSource`/`CCommand` (or ADO) on worker thread | `SEARCH_TERM_PREFIX_ALL` default gives prefix wildcard expansion — good enough fuzzy matching for files. Run on pool thread; per-thread serialization (PowerToys hit OLE DB reader races) |
| `ShellExecuteEx` (SEE_MASK_NOCLOSEPROCESS optional) | WinLaunch | Launch `.lnk`/`.exe`/file with default verb; elevates UWP-bridge `.lnk` targets correctly | Non-blocking; on GUI thread is fine |
| `IApplicationActivationManager::ActivateApplication` | WinLaunch | Launch UWP apps by AUMID (`AO_NONE`; `CLSCTX_LOCAL_SERVER`) | Failure → fall back to `ShellExecuteEx` on the `.lnk` (shell will bridge) |
| `SHGetImageList` / `SHGetFileInfo(SHGFI_SYSICONINDEX)` / `IShellItemImageFactory::GetImage` | IconService | Icon extraction from system image list; `SIIGBF_INCACHEONLY`/`MEMORYONLY` flags to hit Windows' own icon cache without disk work | All must run off the GUI thread (MSDN explicit). `.lnk` icons: read `IShellLink::GetIconLocation`; avoid the shortcut overlay via `SHGFI_SYSICONINDEX` + `ImageList_GetIcon` (Raymond Chen's recipe) |
| `QSystemTrayIcon` + `QMenu` | TrayService | Tray presence; Quit/Settings/Show menu | |
| `HKCU\...\CurrentVersion\Run` | AutostartService | Login autostart without elevation | Write exe path quoted; remove on disable |
| `QLocalServer` (named pipe) | Application (main) | Single-instance: second launch signals first to show window | Standard Qt pattern (Launchy uses SingleApplication lib) |

### Internal Boundaries

| From | To | How | What crosses |
|---|---|---|---|
| HotkeyService | LauncherController | signal (direct) | nothing (just toggle) |
| LauncherController | SearchService | direct call (GUI thread) | query string, requestId |
| SearchService | ResultModel | queued signal | requestId + `QVector<ResultItem>` (plain data) |
| ResultModel | QML ListView | role-based binding | roles (Title, Subtitle, Icon, Score) |
| ResultModel | IconService | direct call (GUI thread) | iconKey string |
| IconService | ResultModel | queued signal | iconKey + `QPixmap` (QPixmap is GUI-thread safe via implicit sharing, copied across the queue — fine at this scale) |
| LauncherController | WinLaunch | direct call | ResultItem (path/AUMID) |
| SettingsService | everyone | direct call (read-only after load) / signals on change | settings values; `hotkeyChanged` triggers HotkeyService re-register |
| TrayService | LauncherController/Settings | signals (Show, Quit, OpenSettings) | actions |

### Suggested Build Order (dependency-driven)

1. **Project skeleton** — CMake + Qt6 Quick app + window with correct flags (`Qt.Tool | FramelessWindowHint`), show/hide, centering. Everything depends on this shell existing; also proves the "opens instantly, no freeze" baseline. (Feeds Phase: window + animation)
2. **SettingsService** — pure C++, zero deps; every later service needs it (hotkey config, theme). (Feeds Phase: settings skeleton)
3. **HotkeyService + LauncherController toggle** — wire real toggle path through the window from step 1; now the app is a usable launcher-shaped object. (Feeds Phase: hotkey)
4. **ResultModel + fake data + QML list** — establishes the model↔view contract and navigation (Enter/Escape/arrow keys) before real search exists. (Feeds Phase: results UI)
5. **AppIndex (Start Menu + UWP) + fuzzy ranker** — tier-1 search; needs 4 for display. Ranker is pure logic → unit-test early. (Feeds Phase: app search)
6. **FileSearchService (Windows Search COM)** — tier-2; depends on the SearchService pipeline from 5; the riskiest component, so it lands after the pipeline is proven. (Feeds Phase: file search)
7. **IconService** — async icons; depends on ResultModel's icon role from 4; polish layer. (Feeds Phase: icons/theme)
8. **TrayService + AutostartService + settings UI** — background services; depend on SettingsService. (Feeds Phase: tray/autostart)
9. **Packaging (installer, LGPL notices)** — last; needs a working binary.

Ordering rationale: each step leaves a runnable, demoable app; the two hard/risky parts (window-focus behavior, Windows Search COM) get dedicated steps where they can be validated in isolation; nothing platform-specific blocks the UI-first phases.

---

## Sources

- PowerToys Run architecture doc (component/data-flow split, IPlugin/IDelayedExecutionPlugin fast-vs-slow search): https://github.com/microsoft/PowerToys/blob/main/doc/devdocs/modules/launcher/architecture.md — HIGH
- PowerToys indexer plugin perf analysis + icon caching measurements (~20% query time; IShellItem not thread-safe): https://github.com/microsoft/PowerToys/issues/7456 — HIGH (first-party measurements)
- PowerToys plugin overview (score-based result ordering, settings.json): https://github.com/microsoft/PowerToys/blob/master/doc/devdocs/modules/launcher/plugins/overview.md — HIGH
- PowerToys Result.cs (`GetSortOrderScore`: WeightBoost + Score + usage): https://github.com/microsoft/PowerToys/blob/86115a54/src/modules/launcher/Wox.Plugin/Result.cs — HIGH
- PowerToys StringMatcher.cs (fuzzy scoring rules): https://github.com/microsoft/PowerToys/blob/b4820c01/src/modules/launcher/Wox.Infrastructure/StringMatcher.cs — HIGH
- Launchy source — production Qt launcher on Windows: window flags `Qt::FramelessWindowHint | Qt::Tool`, `Qt::WindowStaysOnTopHint` toggle, hide-on-focus-lost, `SetForegroundWindow` workaround, tray, QHotkey: https://github.com/samsonwang/LaunchyQt/blob/a70c651b/src/Launchy/LaunchyWidget.cpp — HIGH (working reference implementation)
- Qt 6 docs — QAbstractNativeEventFilter (`windows_dispatcher_MSG` for hotkeys; `qintptr*` signature): https://doc.qt.io/QT-6/qabstractnativeeventfilter.html — HIGH
- QHotkey (Skycoder42) — MIT library wrapping RegisterHotKey + QAbstractNativeEventFilter: https://github.com/Skycoder42/QHotkey — MEDIUM (library choice, verified source)
- Qt 6 docs — QML Window type (`flags`, `requestActivate()`, transient parent): https://doc.qt.io/QT-6/qml-qtquick-window.html — HIGH
- Qt 6 docs — Window Flags example (flag semantics): https://doc.qt.io/qt-6/qtwidgets-widgets-windowflags-example.html — HIGH
- Qt blog — Popups and Menus in Qt Quick 6.8 (`popupType`, Popup.Window caveats): https://www.qt.io/blog/popups-and-menus-in-qt-quick-6.8 — HIGH
- MSDN — ISearchQueryHelper querying the index (CLSID_CSearchManager, SystemIndex, SQL form, prefix expansion): https://learn.microsoft.com/en-us/windows/win32/search/-search-3x-wds-qryidx-searchqueryhelper — HIGH
- MSDN — Using SQL and AQS approaches (OLE DB `Search.CollatorDSO`, ATL example): https://learn.microsoft.com/en-us/windows/win32/search/using-sql-and-aqs-to-query-the-index — HIGH
- MSDN — PackageManager.FindPackagesForUserWithPackageTypes (no admin for current user): https://learn.microsoft.com/en-us/uwp/api/windows.management.deployment.packagemanager.findpackagesforuser — HIGH
- SO — Enumerating Store apps to AUMID in C++ (PackageManager → GetPackageApplicationIds): https://stackoverflow.com/questions/32150759/how-to-enumerate-the-installed-storeapps-and-their-id-in-windows-8-and-10 — MEDIUM (community, but matches MSDN sample shape)
- MSDN — IApplicationActivationManager::ActivateApplication (AUMID launch): https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iapplicationactivationmanager-activateapplication — HIGH
- MSDN — IShellItemImageFactory::GetImage (SIIGBF_INCACHEONLY; "extraction should never be done on a UI thread"): https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitemimagefactory-getimage — HIGH
- MSDN — SHGetFileInfo ("call from a background thread"): https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetfileinfow — HIGH
- Raymond Chen — icon without shortcut overlay (`SHGFI_SYSICONINDEX` + `ImageList_GetIcon`): https://devblogs.microsoft.com/oldnewthing/20110127-00/?p=11653 — HIGH
- MSDN — Launching Applications (ShellExecuteEx verbs): https://learn.microsoft.com/en-us/windows/win32/shell/launch — HIGH
- Qt docs — QSettings (NativeFormat=registry on Windows; IniFormat; custom JsonFormat): https://doc.qt.io/qt-6/qsettings.html — HIGH
- SO/Qt forum — QSettings vs JSON for app settings; `setDefaultFormat(QSettings::IniFormat)` → %APPDATA%: https://stackoverflow.com/questions/42060573/save-ui-settings-with-qsettings-or-qjson — MEDIUM (community consensus, matches Qt docs)
- Qt forum + SO — Qt::Popup dismissal quirks across Qt versions, WM_KILLFOCUS race, Popup swallowing dismissal click: https://forum.qt.io/topic/13845/resolved-qwidget-window-flag-behavior-changed-in-qt-4-8 and https://stackoverflow.com/questions/70738502/qt-ignore-click-when-dismissing-popup-on-windows — MEDIUM (community, multiple sources agree)
- Qt forum — global hotkey in Qt 6 (`nativeEvent` fails, use filter): https://forum.qt.io/topic/144158/global-hotkeys-and-nativeevent-is-not-working-in-qt6 — MEDIUM (community, matches official docs)

---
*Architecture research for: Windows application launcher (Qt6 + QML)*
*Researched: 2026-08-09*
