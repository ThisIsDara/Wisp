---
phase: 06-tray-settings-autostart-packaging
plan: 06-03
subsystem: resident-surface
tags: [tray, qmenum, settings-window, qml-host, qpropertyanimation, qml-controller, win32, qttest]

# Dependency graph
requires:
  - phase: 06-tray-settings-autostart-packaging (06-01)
    provides: AutostartManager HKCU Run-key store (isEnabled/setEnabled) consumed by toggleAutostart
  - phase: 06-tray-settings-autostart-packaging (06-02)
    provides: SettingsWindow.qml + ColorDialog.qml surfaces with the `settingsController` injection contract
  - phase: 02-global-hotkey-toggle / 05-theme-visual-polish
    provides: HotkeyCaptureDialog host + HotkeyManager::setHotkey re-registration path; SettingsStore::setAccent (D-06)
provides:
  - TrayIcon: locked 4-item menu (Open wisp / Settings / Change hotkey… / sep / Quit, D-03) with settingsRequested signal; accent-aware generated disc via setAccent (default #0078D4, D-16 invalid-ignore, white "w" unchanged)
  - SettingsWindow controller: QML host for SettingsWindow.qml + ColorDialog.qml (per-instance beginCreate/setProperty injection), open (center every show → 120ms fade → requestActivate) / instant close, Esc via window event filter, click-away 150ms grace with own-window exemption (launcher pop-over D-04, modal color dialog), hotkey-capture handoff with immediate setHotkey re-registration, accent apply + custom hex commit (T-06-01), autostart toggle with open-time + post-mutation state refresh
  - tst_tray update: 5-entry locked-menu assertions, Settings signal spy, accent setter keeps menu intact + invalid-ignore
  - CMakeLists.txt: SettingsWindow.cpp in wisp_core
affects: [06-04, 06-05]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "QML host controller family (HotkeyCaptureDialog precedent): lazy QQmlComponent load + beginCreate/setProperty per-instance injection — SettingsWindow hosts TWO windows (settings + color dialog) with the same contract"
    - "Controller-owned dismissal: window-level event filter for Esc (surface has no key handler); activeChanged → 150ms single-shot grace; exemption evaluated at TIMEOUT via own-topLevelWindows isActive + modalWindow checks (focusWindow goes null when a foreign window activates)"
    - "QPropertyAnimation on QWindow 'opacity' for the 120ms fade — no QML animation code, surface untouched; opacity reset to 1.0 on close so the next open fades from 0"
    - "Q_PROPERTY + NOTIFY for currentHotkey/autostartEnabled: QML bindings in SettingsWindow.qml re-evaluate on signal — live well text and toggle without QML changes"
    - "Constructor injection of all collaborators (engine + stores + capture dialog) — no service lookup, no store reach-in from TrayIcon (PATTERNS anti-pattern 1)"

key-files:
  created:
    - src/ui/SettingsWindow.h
    - src/ui/SettingsWindow.cpp
  modified:
    - src/tray/TrayIcon.h
    - src/tray/TrayIcon.cpp
    - tests/tst_tray.cpp
    - CMakeLists.txt

key-decisions:
  - "Exemption check implemented with QGuiApplication::topLevelWindows()+isActive()+modalWindow() rather than the plan's literal focusWindow() comparison: focusWindow() returns null the moment a FOREIGN window takes activation, while our own launcher/color-dialog windows keep isActive() true — the top-level scan covers both sides of the exemption (RESEARCH A4 fallback confirmed as the better mechanism)"
  - "Constructor takes injected collaborators (QQmlEngine* + SettingsStore* + AutostartManager* + HotkeyManager* + HotkeyCaptureDialog*) instead of the plan's bare `SettingsWindow(QObject*)` — required to reach the stores and the EXISTING capture dialog; matches the HotkeyCaptureDialog(QQmlEngine*, ...) precedent, keeps 06-04 wiring trivial"
  - "'First focus on the hotkey row' delivered as window-scene forceActiveFocus + locked Tab order (well → strip → Custom… → toggle): the QML surface (06-02, locked) declares no focus hook and adding objectNames would modify it — Tab-from-scene-focus lands on the hotkey well first"
  - "Plan's Q_INVOKABLE list omitted openColorDialog() but 06-02's SettingsWindow.qml calls settingsController.openColorDialog() — added (missing functionality required by the plan's own interfaces)"
  - "Plan task text claimed the change-hotkey label is 'Change hotkey.' in code; the actual source (and the plan's own must_haves/UI-SPEC) has 'Change hotkey…' (U+2026) — copied verbatim from the current source per the 'copy the string verbatim' instruction"

patterns-established:
  - "Multi-window QML host: one controller owns N QQuickWindows, each lazily created + injected on first use"
  - "Grace-exemption evaluated at timeout (not on focus-change): re-activation cancels early; late exemptions (launcher pop-over inside the grace window) keep settings open"
  - "D-16 silent discipline on every boundary: invalid accent → ignored; invalid hex → ignored; store failures → toggle reflects outcome"

requirements-completed: [SYS-01, SYS-03]

# Metrics
duration: 25min
completed: 2026-08-11
---

# Phase 06 Plan 03: Tray Settings Action + SettingsWindow Controller Summary

**The interactive half of the resident surface: tray menu locked to Open wisp / Settings / Change hotkey… / sep / Quit with a `settingsRequested` signal and an accent-aware generated disc (setAccent, default #0078D4, D-16 silent-ignore), plus the SettingsWindow QML-host controller — center-every-show, 120ms fade, instant Esc hide, click-away 150ms grace with the own-window exemption (launcher pop-over and modal color dialog never close settings), hotkey-capture handoff with immediate `HotkeyManager::setHotkey` re-registration, accent apply + T-06-01-safe custom hex commit, and autostart toggle with open-time state refresh — 19/19 tests green.**

## Performance

- **Duration:** 25 min
- **Started:** 2026-08-11T21:24:30Z
- **Completed:** 2026-08-11T21:49:57Z
- **Tasks:** 3
- **Files modified:** 6 (2 created sources, 2 modified tray sources, 1 modified test, 1 modified CMakeLists)

## Accomplishments

- **TrayIcon restructured (D-03 locked order + UI-SPEC tray contract):** menu is now Open wisp → **Settings** (new, wired to `settingsRequested`) → Change hotkey… (string copied verbatim, U+2026 — unchanged) → separator → Quit. The generated 16px QPainter disc's fill now reads from `m_accent` (default `#0078D4`, kept only as the fallback literal); `setAccent()` repaints the pixmap and re-`setIcon`s the tray, silently ignoring invalid colors and same-color no-ops (D-16). TrayIcon never reaches into SettingsStore (PATTERNS anti-pattern 1 — 06-04 wires store → tray).
- **SettingsWindow controller (SYS-03):** a HotkeyCaptureDialog-analog QML host that lazily loads `SettingsWindow.qml` (and `ColorDialog.qml` on first "Custom…" click), injecting itself as `settingsController` per-instance (beginCreate/setProperty — bindings re-resolve, surface stays loadable without it). `open()` = state refresh (D-10) → center on primary `availableGeometry()` (re-applied every show) → opacity 0 → show → 120ms QPropertyAnimation fade (Theme.animFade) → requestActivate → scene focus (first Tab stop = hotkey well). `close()` = instant hide, opacity reset, window kept alive (state persists).
- **Dismissal semantics (D-02/D-04, Pitfall 4):** Esc handled window-level via an event filter on the settings window (the QML surface declares no key handler by design); click-away via `activeChanged` → 150ms single-shot grace; re-activation of the settings window cancels; on timeout the exemption check scans `QGuiApplication::topLevelWindows()` for an active window of OUR process (launcher pop-over) plus `modalWindow()` (staged ColorDialog) — both keep settings open, exactly the D-04 "launcher must pop over settings" contract.
- **Capture handoff + immediate re-registration (success criterion 3):** `openHotkeyCapture()` reopens the EXISTING fullscreen HotkeyCaptureDialog with the current combo; on `accepted` the controller calls `HotkeyManager::setHotkey(QKeySequence)` (validate/persist/atomic swap stays in HotkeyManager; F12/mod-only rejection + conflict balloon untouched) and emits `currentHotkeyChanged` — the QML well text updates live via the NOTIFY binding.
- **Accent + autostart wiring:** `applyAccent(QColor)` → `SettingsStore::setAccent` (D-06 persist + live apply); `commitCustomColor(hex)` parses via `QColor::setNamedColor` with invalid → silently ignored (T-06-01/D-16); `toggleAutostart()` flips `AutostartManager::setEnabled(!isEnabled())` and emits `autostartEnabledChanged` — the getter reads the store live so the toggle reflects the write's OUTCOME (UI-SPEC). Both hotkey and autostart state are re-emitted on every `open()` (D-10).
- **tst_tray updated:** 5-entry locked-menu assertion (labels + separator position), `settingsRequested` QSignalSpy on the Settings action (position-verified between Open wisp and Change hotkey…), accent-setter test (findChild QSystemTrayIcon non-null + icon set + menu intact after `#2EA043`, invalid color silently ignored).
- **Wave gate:** full configure + build (wisp.exe linked, SettingsWindow.cpp compiled into wisp_core) + **19/19 ctest green** (task-2 regression: tst_capture/tst_hotkey/tst_settings/tst_tray all pass).

## task Commits

Each task was committed atomically:

1. **task 1: TrayIcon — Settings action + locked menu order + accent-aware disc** - `0ff0be9` (feat)
2. **task 2: SettingsWindow controller — QML host with dismissal semantics + capture handoff** - `d01d2ed` (feat)
3. **task 3: Wave gate — full build + complete suite green** - no commit (verification-only; tasks 1-2's CMake wiring proved coherent as-is)

## Files Created/Modified

- `src/tray/TrayIcon.h` - `settingsRequested` signal + `setAccent(const QColor&)`; header comment reflects the locked 4-item menu
- `src/tray/TrayIcon.cpp` - Settings action inserted (order locked), `makeTrayIcon(accent)` disc fill from member, setAccent repaint + re-setIcon with D-16 invalid/no-op guards
- `tests/tst_tray.cpp` - 5-entry menu assertions, settings signal spy, accent-setter + invalid-ignore test (new slot)
- `src/ui/SettingsWindow.h` - NEW — QML host controller: open/close, Q_INVOKABLE surface API, Q_PROPERTY currentHotkey/autostartEnabled (NOTIFY), eventFilter, grace/exemption machinery
- `src/ui/SettingsWindow.cpp` - NEW — lazy dual-window hosting, fade animation, Esc filter, 150ms grace + own-window exemption, capture handoff, store wiring
- `CMakeLists.txt` - `src/ui/SettingsWindow.cpp` added to wisp_core (after HotkeyCaptureDialog.cpp)

## Decisions Made

- Exemption check uses top-level-window active scan + modalWindow instead of the plan's literal `focusWindow()` comparison (focusWindow goes null on foreign activation; RESEARCH A4's fallback is strictly more robust).
- Constructor takes injected collaborators (engine, SettingsStore, AutostartManager, HotkeyManager, HotkeyCaptureDialog) — the bare plan signature could not reach the stores or the existing capture dialog; HotkeyCaptureDialog(QQmlEngine*, ...) precedent.
- 120ms fade implemented as a QPropertyAnimation on the window's `opacity` — pure controller-side, the locked QML surface stays untouched.
- "First focus on the hotkey row" = scene forceActiveFocus + locked Tab order (well → strip → Custom… → toggle); no QML objectName edits to the 06-02 surface.
- Live QML updates (well text, toggle) ride on Q_PROPERTY NOTIFY bindings — no QML polling, no reload.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] `openColorDialog()` omitted from the plan's Q_INVOKABLE list but required by the plan's own interface**
- **Found during:** task 2 (SettingsWindow controller)
- **Issue:** The plan's task-2 API list names applyAccent/commitCustomColor/toggleAutostart/openHotkeyCapture but not `openColorDialog()` — while 06-02's SettingsWindow.qml calls `settingsController.openColorDialog()` on the "Custom…" button and the 06-02 SUMMARY's next-phase contract explicitly lists it. Without it the Custom color path is dead.
- **Fix:** Added `Q_INVOKABLE void openColorDialog()` — lazily creates ColorDialog.qml (same injection pattern), recenters, shows, activates.
- **Files modified:** src/ui/SettingsWindow.h, src/ui/SettingsWindow.cpp
- **Verification:** header/cpp compile into wisp_core; full build + 19/19 ctest green
- **Committed in:** d01d2ed (task 2 commit)

**2. [Rule 1 - Bug] Plan task text misdescribes the change-hotkey label**
- **Found during:** task 1 (menu construction)
- **Issue:** The plan's task-1 action says "it is 'Change hotkey.' in code and must not change" — the actual current source has `"Change hotkey…"` (U+2026 ellipsis), as do the plan's own must_haves and UI-SPEC ("Change hotkey…"). The plan's own gate would have failed had the literal `"Change hotkey."` been used.
- **Fix:** Copied the string verbatim from the current source ("Change hotkey…", U+2026) per the plan's own "copy the string verbatim" instruction. No behavior change; test assertions match the real label.
- **Files modified:** src/tray/TrayIcon.cpp, tests/tst_tray.cpp
- **Verification:** byte-level check (U+2026 confirmed in both original and new source); tst_tray green
- **Committed in:** 0ff0be9 (task 1 commit)

---

**Total deviations:** 2 auto-fixed (1 missing critical, 1 plan-text inaccuracy)
**Impact on plan:** Both fixes were required for the plan's own acceptance gates and the 06-02 surface contract; no scope creep, no behavior change.

## Issues Encountered

- None. The build environment (vcvars64 via build.ps1 pattern) worked cleanly; no running wisp.exe held the output file this time.

## User Setup Required

None - no external service configuration required. (Manual dismissal-semantics verification is a 06-04/UAT checkpoint: open settings, Esc hides; click away hides after grace; hotkey summons launcher over open settings.)

## Next Phase Readiness

- **06-04 (main.cpp wiring):** ready to construct `SettingsWindow(&engine, &settingsStore, &autostart, &hotkeys, &capture, &app)` and connect `tray.settingsRequested → settingsWindow.open`; call `tray.setAccent(settingsStore.accent())` at startup + on `accentChanged` (live disc repaint); no further SettingsWindow API needed. The controller compiles into wisp_core but is NOT yet referenced by any binary — expected until 06-04 wires it (not a stub: full implementation, injected collaborators).
- **CMakeLists.txt:** wisp_core coherent; 19 tests registered and green.
- **Known deferral to 06-04:** `settingsVisibleChanged` signal emitted but unconnected (main.cpp doesn't need it yet); window-scene-focus interpretation of "first focus on the hotkey row" is visual-check material for the 06-04 manual smoke.
- No blockers.

## Known Stubs

None. The controller is fully implemented; its instantiation and tray/store wiring are deliberately deferred to 06-04 (plan contract — main.cpp is that plan's file).

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: none | — | No new surface beyond the plan's register: no network/auth/file access added; commitCustomColor bounded by QColor::setNamedColor with invalid → ignored (T-06-01 mitigated as planned); tray menu and QML calls are in-process signals only (T-06-06 accepted as planned). |

---

*Phase: 06-tray-settings-autostart-packaging*
*Completed: 2026-08-11*

## Self-Check: PASSED

All 6 modified/created files verified on disk (TrayIcon.h, TrayIcon.cpp, tst_tray.cpp, SettingsWindow.h, SettingsWindow.cpp, CMakeLists.txt); both task commits (0ff0be9, d01d2ed) verified in git log; wave gate: configure + build + 19/19 ctest green.
