---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
stopped_at: Phase 6 UI-SPEC approved
last_updated: "2026-08-14T00:47:03.247Z"
last_activity: 2026-08-14 -- Phase 07 planning complete
progress:
  total_phases: 8
  completed_phases: 7
  total_plans: 35
  completed_plans: 30
  percent: 86
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-09)

**Core value:** The launcher must open and launch in under a second with buttery-smooth animation. Speed and feel are the product.
**Current focus:** Phase 06 — tray-settings-autostart-packaging

## Current Position

Phase: 06 (tray-settings-autostart-packaging) — EXECUTING
Plan: 1 of 5
Status: Ready to execute
Last activity: 2026-08-14 -- Phase 07 planning complete

Phase history: Phase 02 (global-hotkey-toggle) COMPLETE (3/3 waves, ctest 5/5); Phase 03 (app search & result model) COMPLETE (5/5 waves, ctest 10/10, vertical slice user-approved: calc→Calculator #1, instant typing/nav/launch, UAC-cancel quiet, UWP admin-refusal hint, centered opens, constant-speed key scrolling — user-reported defects all resolved: top-left placement, Esc/click-away after first close, query persistence, hover vs key auto-repeat); Phase 04 (file search) COMPLETE (5/5 plans, ctest 13/13, human-approved 8/8: merged .exe/folder rows, Enter launch, Ctrl+Enter Explorer reveal, Ctrl+Shift+Enter silent-normal, folder ▸ glyph, Add executable… persistence, indexer-disabled status row, debounce feel; checkpoint note added vibrant left selection-bar indicator); Phase 05.1 (catalog curation) COMPLETE (4/4 plans, ctest 17/17, user-approved: curated allowlist default, Ctrl+H hide, Show hidden (N) footer, right-click Hide/Unhide menu, persistence, Add-executable escape hatch; code review 2 HIGH/3 MEDIUM/5 LOW all fixed; verification passed)

Progress: [███████████░░] 83%

## Performance Metrics

**Velocity:**

- Total plans completed: 25
- Average duration: —
- Total execution time: —

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

*Updated after each plan completion*
| Phase 01 P01 | 38 | 3 tasks | 7 files |
| Phase 01 P02 | 75 | 3 tasks | 5 files |
| Phase 03 P01 | 10 min | 3 tasks | 8 files |
| Phase 03 P02 | 45 min | 3 tasks | 6 files |
| Phase 03 P03 | 4 min | 1 task | 4 files |
| Phase 03 P04 | 40 min | 2 tasks | 6 files |
| Phase 03 P05 | 90 min | 3 tasks | 12 files |
| Phase 04-file-search P01 | 40min | 2 tasks | 5 files |
| Phase 04-file-search P02 | 20min | 2 tasks | 5 files |
| Phase 04-file-search P04 | 27min | 2 tasks | 3 files |
| Phase 04-file-search P03 | 6min | 2 tasks | 9 files |
| Phase 04-file-search P05 | 25min | 3 tasks | 3 files |
| Phase 5 P1 | 55min | 3 tasks | 6 files |
| Phase 5 P2 | 30min | 2 tasks | 4 files |
| Phase 05-theme-visual-polish P3 | 14min | 2 tasks | 4 files |
| Phase 05-theme-visual-polish P4 | 17min | 3 tasks | 8 files |
| Phase 05-theme-visual-polish P5 | 32min | 3 tasks | 4 files |
| Phase 05.1 P1 | 40min | 2 tasks | 4 files |
| Phase 05.1 P2 | 35min | 2 tasks | 3 files |
| Phase 05.1 P3 | 33min | 2 tasks | 3 files |
| Phase 05.1 P4 | 150min | 3 tasks (2 auto + 1 checkpoint) | 4 files |

## Accumulated Context

### Roadmap Evolution

- Phase 05.1 inserted after Phase 5: Catalog curation - show real apps and games only (URGENT) — COMPLETE 2026-08-11

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Phase 05.1]: Default catalog = curated ALLOWLIST (~280 well-known apps/games tokens in CurationRules), not a blocklist — user-directed at checkpoint ("around 200-300 handpicked automatically"); user hide/show overrides always beat rules (shownIds/hiddenIds consulted before matches())
- [Phase 05.1]: hideSelected MARKS the entry hidden in place (never removes) — removal made unhide impossible and broke the footer count; the visibility branch (hidden && !showHidden) in every order-building site does the filtering
- [Phase 05.1]: QML bindings never re-evaluate on Q_INVOKABLE method calls — QML-visible state must be Q_PROPERTY with NOTIFY (hiddenCount bug found via user report "nothing above Add executable")
- [Phase 05.1]: Context menu = shell-owned IN-WINDOW overlay (delegate signal + coords via mapToItem), never a Popup window — delegate-scoped Popups landed off-position on the scale-transformed delegates; QQC Menu resisted compact styling (codebase convention: concrete widgets only)
- [Phase 05.1]: CurationStore skips INI-hostile ids ('=', '[', ']') and surfaces sync failures via qWarning — a truncated/corrupt registry line must never silently void a hide

- [Phase 1]: Qt 6.11.1 open-source, dynamic linking only (LGPL) — do NOT use 6.8 LTS (commercial-only patches); Qt owns DPI awareness (PMv2), never call SetProcessDpiAwareness*
- [Phase 1]: Window = `Qt.Tool | FramelessWindowHint`, NOT `Qt::Popup` (popup swallows dismissal click, breaks IME); project structure per ARCHITECTURE.md (`src/win/` as Win32/COM firewall)
- [Phase 1]: Deploy script (`windeployqt --qmldir`) + `THIRD-PARTY-NOTICES.txt` stub seeded now, verified clean-VM in Phase 6
- [Phase 1]: Theme.qml singleton must be registered via `QT_QML_SINGLETON_TYPE` source property — Qt 6.11 qt_add_qml_module does NOT auto-detect `pragma Singleton`; unregistered singletons silently yield undefined properties
- [Phase 1]: Close lifecycle = closing-flag pattern: reject only the FIRST close (so the 140ms animation plays), then hide() + accepted root.close() + Qt.quit() in closeAnim.onFinished. Windows refires rejected closes forever and Qt.quit() alone never exits after a rejected close
- [Phase 1]: Keyboard focus lives on an Item, never the QML Window (Window has no `focus` property); Keys.onEscapePressed lives on the shell Item (focus: true)
- [Phase 1]: Static QML module (`qt_add_library(wisp_qml STATIC)`) requires explicitly linking the generated plugin target `wisp_qmlplugin` into exe and tests — otherwise "No module named wisp found" at runtime (registration objects never linked)
- [Phase 1]: `qt_policy(SET QTP0001 NEW)` — module resources at `:/qt/qml/<Module>` (deploy-safe; without it deployed apps fail while dev works)
- [Phase 1]: deploy.ps1 writes qt.conf (windeployqt 6.11 does not emit one); deploy.ps1 must stay ASCII-only (PS 5.1 parse trap); wipe build dirs after CMake restructures (stale qrc/registrations mask bugs)
- [Phase 2]: Focus sequence show→raise→requestActivate deferred off the WM_HOTKEY handler; fullscreen guard via SHQueryUserNotificationState; conflict surfacing via tray notification
- [Phase 2]: `WinHotkey` must inherit `QObject, QAbstractNativeEventFilter` (multiple inheritance) — `Q_OBJECT` requires QObject as a base; QAbstractNativeEventFilter alone is not a QObject
- [Phase 2]: `wisp_core` STATIC lib (`src/win/*` + `src/core/*`) linked by both wisp and tst_hotkey — app/test sharing without exe-object duplication
- [Phase 2]: HotkeyManager contract: `start()` performs the boot registration; `setHotkey()` swaps while live (register-new-revert-old). Calling start() twice, or setHotkey() before start(), self-conflicts with 1409 — tests must mirror the real boot flow
- [Phase 2]: LauncherController owns ALL visibility policy (no QML logic); QPointer<QQuickWindow> + invokeMethod(dismiss/hideNow/requestActivate) — window-light tests need neither window nor hotkey
- [Phase 2]: MainWindow `closing` flag must reset on hide (onVisibleChanged !visible) — residence exposed that Phase-1 quit-on-close masked
- [Phase 2]: New src/core/* sources must be registered in wisp_core's source list — forgotten LauncherController.cpp produced 8 LNK2019s; direct `cmake --build` without vcvars env fails LNK1104 (d3d11.lib) — always build via build.ps1
- [Phase 2]: ctest self-sufficient since 2026-08-10: CMakeLists `ENVIRONMENT_MODIFICATION` prepends Qt bin dir — no manual PATH prep needed (older note: build.ps1 scoped it to its own cmd session); QtTest stdout gets swallowed under bare pwsh pipes — use Start-Process file redirection to diagnose
- [Phase 2]: `QObject::setParent` on a widget whose parent is a non-widget QObject ASSERTS (`!d->isWidget`, qobject.cpp:2265) — TrayIcon owns its QMenu via explicit dtor + `menu()` accessor, no parent
- [Phase 2]: QML `component` (inline type declarations) rejected by Qt 6.11 qmlcachegen/qmllint — duplicate concrete widgets instead
- [Phase 2]: HotkeyCaptureDialog lives in wisp_core (tests link it); wisp exe target must NOT also compile it; wiring-order grep must take the LAST `hotkeys.start()` (comments match First)
- [Phase 5]: Backdrop blur is v2 (VISU-04) — Phase 5 focuses on dark theme + accent + icons; if blur is attempted, use QtQuick.Effects MultiEffect, never Qt5Compat
- [Phase 6]: VISU-03 (accent picker) ships with the settings window (SYS-03); Phase 5 pre-wires the accent system with a default
- [Phase 03]: MatchRangesRole QML shape locked for 03-05: QVariantList of two-int lists [{start,length}, ...] per contiguous matched run, positions into the original displayName (documented in ResultsModel.h)
- [Phase 03]: FuzzyMatcher tier ladder as binary thresholds (1000/800/600/400) with bonuses capped below the 200-point tier gap - exact > prefix > boundary > subsequence holds by construction, not just for fixtures
- [Phase 03]: Case-insensitive matching via per-char QChar::toCaseFolded() (not QString::toCaseFolded()) - avoids multi-char fold (ss) position-mapping hazards; match ranges stay in original-string space
- [Phase 03]: FuzzyMatcher subsequence scan prefers a word-boundary occurrence for the FIRST matched char (tier-determining), then greedy-leftmost; boundary-tier never missed when a later boundary occurrence exists
- [Phase 03]: UWP AUMID comes from AppListEntry.AppUserModelId() (26100 SDK projection has NO Package.Applications); buildAumid(PFN!AppId) seam kept + unit-tested as the format contract
- [Phase 03]: C++/WinRT via Windows SDK headers (no NuGet): cppwinrt include resolved from $env{WindowsSdkDir} (vcvars64 inside build.ps1) + `target_link_libraries(wisp_core PRIVATE windowsapp)` for RoGetActivationFactory — every C++/WinRT consumer needs windowsapp
- [Phase 03]: NEVER use QDir::NoSymLinks when walking *.lnk on Windows — Qt 6.11 classifies .lnk as links for that filter and silently hides every shortcut (real bug found by tst_enum; fix commit 05af3a6)
- [Phase 03]: Windows Defender quarantines MZ-magic junk files written to TEMP fixtures at write time — test garbage must not start with a PE signature
- [Phase 03]: QtTest output on this machine is swallowed by pwsh pipes AND cmd redirection AND Start-Process redirects; use QtTest's own `-o <file>,txt` (bypasses console entirely)
- [Phase 03]: QtConcurrent::run + QFutureWatcher worker route over a dedicated QThread — watcher lives on the UI thread so dedupe/sort/swap always run there; ScanResult{entries,errorCount} carries per-scanner failure counts across the thread boundary
- [Phase 03]: Snapshot storage = single QVector under QMutex (mutable for const entries()); writer swaps under lock so the swap IS the atomic step (T-03-03-01); m_buildInFlight/m_lastBuilt/m_interval are UI-thread-only
- [Phase 03]: Catalog worker initializes its COM apartment with CoInitializeEx(COINIT_MULTITHREADED) (same call winrt::init_apartment(multi_threaded) makes) with S_FALSE/RPC_E_CHANGED_MODE reuse discipline — keeps WinRT headers confined to WinUwpEnumerator.cpp
- [Phase 03]: Launcher injectable carries the ResultReporter param (03-04 plan-authorized header adjustment): controller passes its reporter at call time so outcome classification stays controller-owned (D-11/D-13 policy testable without OS calls)
- [Phase 03]: ShellExecuteEx quiet-error discipline: SEE_MASK_FLAG_NO_UI + ERROR_CANCELLED(1223)/SE_ERR_ACCESSDENIED(5) map to CancelledByUser - quiet no-op, zero signals (D-11 no error-spam); hProcess closed without wait (D-13 instant path)
- [Phase 03]: CLSID_ApplicationActivationManager embedded as local GUID constant 45BA127D-10A8-46EA-8AB7-56EA9078943C (verified vs 10.0.26100.0 ShObjIdl_core.h) - header declares EXTERN_C so the value lives in uuid.lib; IID via __uuidof
- [Phase 03]: Keys.forwardTo: [shell] on the search TextField — ONE shell-side Keys block owns nav/Enter/Escape; accepted keys never reach the caret, character keys fall through (verified live: ↑ while typing moves selection, not caret)
- [Phase 03]: ListView currentIndex: resultsModel.selectedIndex — the model is the single selection truth; delegate paints selection (no highlight component, highlightFollowsCurrentItem: false); hover never scrolls, keyboard never animates (followSelection() only from key handlers)
- [Phase 03]: Hover/keyboard arbitration = 250ms key-idle timestamp gate: nav keys stamp resultsView.lastKbPressMs; delegate MouseArea ignores hover while Date.now()-stamp < 250ms (auto-repeat owns selection regardless of cursor position); hover resumes after keys idle
- [Phase 03]: QML x/y loses to WM placement for hidden frameless tool windows on first show — deferred (singleShot 0) C++ setPosition on screen()->availableGeometry() in showWindow() is the centering that works (verified live at (624,300) on 1920×1080)
- [Phase 03]: Controller state truth = QQuickWindow::isVisible(), never a C++ side flag — QML hides the window on Esc/click-away without notifying the controller; desynced flags cause ghost dismissals + stuck closing flag
- [Phase 03]: WM_HOTKEY ingested on a dedicated pump thread (WinHotkey) — thread-queue WM_HOTKEY delivery depends on window-message pumping; fragile once the QML slice grew the event loop (hotkey died after first open); fullscreen guard must be focus-aware (D-02.3 amendment) or it blocks the re-open after launch dismissal
- [Phase 4]: CSearchManager must be created with CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER - Win11 registers it out-of-process only (empty InprocServer32 stub); CLSID_CSearchManager symbol does not link - use __uuidof
- [Phase 4]: System.IsFolder binds as DBTYPE_WSTR ('True'/'False'), not DBTYPE_BOOL - provider rejects BOOL with BADBINDINFO
- [Phase 4]: DBBINDING value/length slots must be 16-byte aligned, status slots 8-byte aligned - compact 4-aligned layout rejected with BADBINDINFO (verified live)
- [Phase 4]: 'AND ' prefix required on put_QueryWhereRestrictions fragment (provider appends verbatim after CONTAINS) - kept out of the locked contract string
- [Phase 04]: kDebounceMs locked at 150ms (D-12 range 120-150ms) — single-shot restart so the LAST text wins — D-12 roadmap range; 150ms is the feel-optimal top of range
- [Phase 04]: Generation check in onFinished is defense-in-depth layer ONE; ResultsModel::setFileResults (04-04) re-checks — D-15 stale results structurally cannot render
- [Phase 04]: Worker lambda wraps seam calls in try/catch — a throwing seam degrades to Unavailable instead of escaping QtConcurrent — AppCatalog discipline; nothing may escape the pool thread
- [Phase 04]: StatusFn returns FileSearchState ordinal; stateFromOrdinal maps defensively with default to Ok — Ordinal order mirrors WinSearchQuery::IndexerState (04-01)
- [Phase ?]: m_order refactored from QVector<int> to QVector<Row {int entryIndex; bool fromFiles}>
- [Phase ?]: mergeFiles collects app candidates filtering fromFiles==false (stale-row leak fix)
- [Phase ?]: kPathMatchScore=100 base tier below every name match (D-07)
- [Phase ?]: File arrival never resets selection; clamp only (D-02)
- [Phase 04-file-search]: LaunchHistory::m_settings is a value member constructed via makeSettings() factory (guaranteed elision) — a ternary member-init cannot compile on MSVC (QSettings copy ctor deleted, C2280); HotkeyManager's pointer analog does not apply
- [Phase 04-file-search]: recordLaunch counts via launchCount(path)+1 — the path-based accessor is the single counting point (idempotent with key-passed variant, cleaner contract)
- [Phase 04-file-search]: Comment wording keeps the literal 'runas' string out of LaunchController.cpp entirely — the grep gate demanded zero occurrences; the D-05 mapping provably never uses the elevation verb (effectiveElevated=false for Source::File, test-proven)
- [Phase 04-file-search]: D-05 elevation mapping: effectiveElevated = (source == File) ? false : elevated, computed in launchEntry BEFORE the launcher call — the launcher never sees an elevated file row
- [Phase 04-file-search]: Reveal policy mirrors the ResultReporter table exactly: Launched -> dismiss (D-13), CancelledByUser -> quiet, Failed -> launchFailed(displayName)
- [Phase 04-file-search]: Dual-pipeline typing in QML: ONE onTextChanged routes to resultsModel.setQuery AND fileSearch.setQuery — apps instant, files debounced (D-12 feel contract lives in the QML line)
- [Phase 04-file-search]: launchFromKey branch order elevated (Ctrl+Shift) > reveal (Ctrl) > normal — reveal is a designed no-op for Lnk/Uwp (04-03 policy), so Ctrl+Enter on app rows changes nothing (T-04-15)
- [Phase 04-file-search]: Status row = full-list-area Item overlay gated on !fileSearch.indexerOk (D-18 non-selectable); emptyState additionally gated on fileSearch.indexerOk so the overlay owns the space only when the list is empty
- [Phase 04-file-search]: Selection indicator (checkpoint note 2026-08-10) = left-edge accentLight bar, Theme.spaceSm (8px — 4px reads thin against the accent selection bg), visible only on ListView.isCurrentItem; hover rows never show it; token-only
- [Phase 4]: QSettings is NOT safe for concurrent access — worker-thread trackedExecutables reads the same QSettings instance the UI-thread launch path writes; WR-01 fix = QMutex guard in LaunchHistory (QMutexLocker around every accessor)
- [Phase 4]: OLE DB row reads must check DBSTATUS_S_OK on every column and honor the length slots — the row buffer is reused across GetData calls, so error statuses/oversized values leave stale or unterminated bytes (WR-02)
- [Phase 4]: Generation counters alone don't catch stale TEXT — a slow query from an older keystroke can land during the debounce window of a newer one; carry the query text in resultsReady and drop on mismatch (WR-03, defense in depth layer 3)
- [Phase 4]: Qt 6 QSignalSpy::wait() only waits for a NEW signal (size > origCount) — a signal that landed during a preceding qWait makes wait() time out; assert delivery inside the quiet window instead (WR-05)
- [Phase 4]: ctest on Windows needs Qt's bin dir on PATH to find Qt6Testd.dll etc. — build.ps1 scopes it to its own session, so bare ctest fails 0xc0000135; fixed in CMakeLists with `set_tests_properties(... ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")` — plain `ctest --test-dir build/dev` works from any shell now
- [Phase 5]: gsd-tools CLI lives at `~/.config/opencode/get-shit-done/bin/gsd-tools.cjs` (not .planning/.gsd-tools) — no `state.record-session`/`phase.complete`/`state_phase_roadmap` subcommands exist; STATE.md session fields are edited manually as the established fallback
- [Phase 5]: Icon extraction runs on Qt's dedicated provider thread with per-call MTA COM; UI thread never enters COM — STACK HIGH rule, D-02/D-16
- [Phase 5]: IconKey id transport via QUrl percent-encoding; wave-0 spike proved lossless for ; : | # % space ! backslash; QML must encodeURIComponent(iconKey)
- [Phase 5]: UWP logos resolve AppxManifest Square44x44Logo (namespace-tolerant) -> SHLoadIndirectString @{PFN?ms-resource:...} -> scale-variant probe -> DisplayInfo.GetLogo fallback
- [Phase 5]: PackageManager API is FindPackage(packageFullName) — GetPackageByFullName / FindPackageByPackageFullName do not exist in the 26100 cppwinrt projection
- [Phase 5]: Qt 6.11: QPixmap::fromWinHICON / qwinfunctions.h removed; use QImage::fromHICON / fromHBITMAP with copy() before DeleteObject
- [Phase 5]: REQUIREMENTS.md VISU-02 stays Pending until phase completion — LAUN-02 precedent: requirements marked complete at phase close, not per-plan
- [Phase 05-theme-visual-polish]: IconCache LRU order kept in a single QList<QString> (removeOne+append on touch) — O(n) at n <= 500 fine; one canonical order list keeps map/order provably consistent
- [Phase 05-theme-visual-polish]: IconCache size() uses m_order.size() — the order list is the eviction authority; map and order stay in lockstep by construction
- [Phase 05-theme-visual-polish]: tst_iconcache grep gate counts 5 not the plan's 3 in CMakeLists — idiomatic CMake wins (05-01 precedent); gate deviation documented, no build-file contortion
- [Phase 05-theme-visual-polish]: SettingsStore derives QObject (D-14) — the QML Connections onAccentChanged wiring in 05-05 REQUIRES the NOTIFY signal; only the makeSettings factory + QSettings discipline copied from LaunchHistory, not the plain-class shape
- [Phase 05-theme-visual-polish]: SettingsStore accent() is a LIVE read (readAccent() per call) — no cached member, no staleness after external INI edits; QML consumes once at startup + via signal
- [Phase 05-theme-visual-polish]: SettingsStore setAccent persists c.name() then sync() then emits — persist BEFORE notify so the QML re-read in onAccentChanged always sees the persisted value; invalid colors silently ignored (no-op, no notify)
- [Phase ?]: [Phase 05-04]: iconKey role follows the parseKey grammar verbatim — Lnk iconRef 'path;index' else 'path:'+targetPath; File 'path:'+targetPath; Uwp 'uwp:PFN|appId' ('' -> QML monogram)
- [Phase ?]: [Phase 05-04]: IconProvider is pure Qt + std::function extraction seam (main.cpp binds WinIconExtractor::extract) — Win32 never enters src/core; provider runs on Qt's provider thread, LRU-first, failures never cached (D-16)
- [Phase ?]: [Phase 05-04]: UWP appId derived by splitting the AUMID at '!' (PFN!AppId OS contract, buildAumid-locked) — no extra WinRT surface; IPackageId.FullName() is the 26100-projection member (PackageFullName absent)
- [Phase ?]: Qt.escape() is absent in Qt 6.11 (verified against the documented Qt global-object method list during implementation) - T-05-18 escaping implemented via the plan's own sanctioned alternative (replace &,<,> in a local escapeText()); code comment keeps the plan reference for gate traceability
- [Phase ?]: chipBg is an accent-change-reactive BINDING (accent at 20% blended over the surface, opaque) rather than strictly once-at-startup - same UI-SPEC line-109 math, strictly more consistent when Phase 6 live-changes the accent (D-15 belt)

### Pending Todos

From .planning/todos/pending/ — ideas captured during sessions.

None yet.

### Blockers/Concerns

Open questions carried from research (resolve during phase planning, not now):

- [Phase 5]: Mixed-DPI IShellItemImageFactory behavior — verify; UWP icon indirect-string resolution — spike

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| Feature | Backdrop blur (VISU-04) — v2 | Deferred | 2026-08-09 |
| Feature | Recency ranking, recent apps (LAUN-07/08) — v2 | Deferred | 2026-08-09 |
| Feature | Copy full path (LAUN-09) — v2 | Deferred | 2026-08-09 |

## Session Continuity

Last session: 2026-08-11T16:06:06.956Z
Stopped at: Phase 6 UI-SPEC approved
Resume file: .planning/phases/06-tray-settings-autostart-packaging/06-UI-SPEC.md
