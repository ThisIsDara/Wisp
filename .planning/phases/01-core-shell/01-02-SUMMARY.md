---
phase: 01-core-shell
plan: 02
subsystem: ui-shell, qml, animation
tags: [qt6, qml, singleton, theme, animation, shadow, frameless]

requires:
  - phase: 01-core-shell (plan 01)
    provides: Buildable Qt 6.11.1 + QML skeleton (wisp.exe, URI wisp, 672x432 transparent Tool window)
provides:
  - Theme.qml pragma Singleton carrying every UI-SPEC design token (7 colors, 4px spacing grid 4-64, 44px row + 12px radius exceptions, Segoe UI Variable typography, 150/140ms OutCubic/InCubic animation, 0.96 scale, 672x432 geometry)
  - Pre-rendered 672x432 ARGB shadow PNG (qml/assets/shadow.png) + one-shot regenerator script — zero runtime blur cost
  - Full animated shell: 640x400 surface, 12px radius, 1px #3F3F46 border, static shadow, open 150ms opacity+Scale 0.96->1.0 from onVisibleChanged, close 140ms with hide-then-exit lifecycle
  - Verified ESC and Alt+F4/WM_CLOSE dismissal paths (both exit the process cleanly)
affects: [01-core-shell (plan 03 tests), 03, 05]

tech-stack:
  added: [System.Drawing (shadow PNG generation, build-time only)]
  patterns: [pragma Singleton registered via QT_QML_SINGLETON_TYPE source property, single animated subtree with Scale transform, rejected-close + closing-flag re-close lifecycle]

key-files:
  created: [qml/Theme.qml, qml/assets/shadow.png, scripts/generate-shadow.ps1]
  modified: [qml/MainWindow.qml, CMakeLists.txt]

key-decisions:
  - "Qt 6.11 qt_add_qml_module does NOT auto-register pragma Singleton files — must set_source_files_properties(QT_QML_SINGLETON_TYPE TRUE); without it every Theme.* token resolves undefined"
  - "closeAnim uses Animation's `finished` signal (bare onCompleted is invalid on Animation objects; Component.onCompleted would fire at object creation, not completion)"
  - "Window has no `focus` property (QML Window type); keyboard focus lives on an Item — Keys.onEscapePressed moved onto the shell Item with focus:true"
  - "Rejected closes on Windows refire forever: onClosing must reject only the first close, then let closeAnim.onFinished's root.close() be accepted (closing flag), else the app never exits"
  - "Qt.quit() alone did not terminate the app after a rejected close; hide() + accepted root.close() (quitOnLastWindowClosed) + Qt.quit() belt-and-braces"
  - "Qt 6.11 Key_Escape == 0x01000000 (key enum reorganized) — key-injection test logs must not assume 0x0100001B"
  - "window->setFlag(Qt::WA_TranslucentBackground) skipped again per plan-01 deviation (QWidget-only API; translucency via color:'transparent' + setDefaultAlphaBuffer(true))"

patterns-established:
  - "Theme singleton consumes tokens; MainWindow consumes Theme — zero literals for color/size/duration/easing in view code"
  - "Debug instrumentation via qInstallMessageHandler -> temp file log + window eventFilter probe; removed after diagnosis (WIN32 app has no stderr)"
  - "Headless key verification: WScript AppActivate + SendKeys for real input; PostMessage WM_CLOSE for the close path"

requirements-completed: []

# Metrics
duration: 75min
completed: 2026-08-09
---

# Phase 01 Plan 02: Theme Tokens + Animated Shell Summary

Theme.qml pragma Singleton ships every UI-SPEC design token; the window is now a full shell — 640x400 #1E1E1E 12px-radius surface with 1px #3F3F46 border, pre-rendered 672x432 soft shadow, 150ms OutCubic opacity+Scale open, 140ms InCubic close — with both ESC and Alt+F4 dismissal paths verified end-to-end to exit the process.

## Performance

- **Duration:** 75 min
- **Started:** 2026-08-09T08:20:00Z
- **Completed:** 2026-08-09T09:35:00Z
- **Tasks:** 3
- **Files modified:** 5

## Accomplishments
- Theme.qml singleton with ALL UI-SPEC tokens (colors, spacing grid, row/radius exceptions, typography, animation durations/easings, geometry) — verified no literals remain in MainWindow.qml
- Pre-rendered shadow PNG (672x432 ARGB, soft 16px halo) via regenerable System.Drawing script; static Image in QML — zero per-frame blur cost
- Full animated shell per VISU-01: 150ms open (opacity 0->1 + Scale 0.96->1.0, OutCubic, from onVisibleChanged), 140ms close (InCubic, hide only in onFinished)
- ESC dismissal verified with real input (SendKeys); Alt+F4/WM_CLOSE verified via PostMessage; both exit the process cleanly with the closing-flag re-close pattern

## task Commits

1. **task 1: Theme.qml token singleton** - a3085b9 (feat, combined with tasks 2+3)
2. **task 2: shadow PNG generator + asset** - a3085b9 (feat)
3. **task 3: MainWindow.qml animated shell + dismiss paths** - a3085b9 (feat)

**Plan metadata:** plan 01-02 had no separate docs commit (plan authored in phase 01 planning batch)

## Files Created/Modified
- `qml/Theme.qml` - pragma Singleton QtObject with every UI-SPEC token; registered via QT_QML_SINGLETON_TYPE
- `qml/assets/shadow.png` - pre-rendered 672x432 ARGB soft shadow (black 45%, 16px)
- `scripts/generate-shadow.ps1` - one-shot regenerator (4x render, HighQualityBicubic downscale); ASCII-only per PS 5.1 rule
- `qml/MainWindow.qml` - full shell: transparent Window 672x432, shell Item + Scale transform, static shadow Image, surface Rectangle, openAnim/closeAnim ParallelAnimations, Keys.onEscapePressed + onClosing dismiss paths
- `CMakeLists.txt` - QML_FILES += Theme.qml, RESOURCES += shadow.png, QT_QML_SINGLETON_TYPE property

## Decisions Made
- Register the singleton explicitly (Qt 6.11 does not auto-detect `pragma Singleton` in qt_add_qml_module) — without it every Theme.* property resolves undefined and the window silently renders at default size with defaults
- Close animation uses Animation's `finished` signal; `onCompleted` is invalid on Animation types and Component.onCompleted would fire at creation, not completion
- Escape handled on the shell Item (Window has no `focus` property; key delivery requires an item with active focus)
- Windows refires close requests against a rejected close: guard with `closing` flag — first close rejected + animated, re-close from onFinished accepted
- App exit via hide() + accepted root.close() (quitOnLastWindowClosed) with Qt.quit() as backup

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Won't Work] Plan QML used `focus: true` on Window**
- **Found during:** task 3 (shell rewrite)
- **Issue:** QML Window type has no `focus` property; load failed with "Cannot assign to non-existent property focus"
- **Fix:** Removed; key focus lives on the shell Item (focus: true) which also hosts Keys.onEscapePressed
- **Files modified:** qml/MainWindow.qml
- **Verification:** QML loads; ESC path verified via SendKeys
- **Committed in:** a3085b9

**2. [Rule 1 - Won't Work] Plan QML used `onCompleted` on ParallelAnimation**
- **Found during:** task 3 (shell rewrite)
- **Issue:** Animations have no `onCompleted` (bare or attached); load failed with "Cannot assign to non-existent property onCompleted"
- **Fix:** Switched to Animation's `finished` signal — the contract's intent (animation completion -> hide)
- **Files modified:** qml/MainWindow.qml
- **Verification:** "close finished" fires exactly once per dismiss during testing; window hides then exits
- **Committed in:** a3085b9

**3. [Rule 1 - Won't Work] Rejected close loops forever; Qt.quit() never exits**
- **Found during:** task 3 verification (ESC and WM_CLOSE tests stayed alive)
- **Issue:** close.accepted = false + Qt.quit() in onFinished: Windows refires the close request against the rejected window; Qt.quit() alone (even without the loop) did not terminate the process
- **Fix:** `closing` flag: onClosing rejects only the first close; closeAnim.onFinished does hide() + root.close() (now accepted) + Qt.quit() belt-and-braces
- **Files modified:** qml/MainWindow.qml
- **Verification:** WM_CLOSE -> 2 close events (reject, then accept) -> process exits; ESC -> same clean exit
- **Committed in:** a3085b9

**4. [Rule 1 - Won't Work] Theme singleton never registered**
- **Found during:** first launch after rewrite (window at default size; QML logged "Unable to assign [undefined]" for every Theme.* use)
- **Issue:** Qt 6.11 qt_add_qml_module reads QT_QML_SINGLETON_TYPE source property; nothing scans for `pragma Singleton` (verified: zero pragma references in Qt6QmlMacros.cmake; generated qmldir had plain `Theme 0.0` entry)
- **Fix:** set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE) -> qmldir now has `singleton Theme 0.0`
- **Files modified:** CMakeLists.txt
- **Verification:** all "Unable to assign [undefined]" errors gone; theme-driven geometry renders
- **Committed in:** a3085b9

**5. [Rule 1 - Won't Work] Keep window->setFlag(Qt::WA_TranslucentBackground) in main.cpp**
- **Found during:** task 3 action step
- **Issue:** QWidget-only API (invalidated in plan 01-01, deviation #3); does not compile for QQuickWindow
- **Fix:** Skipped — translucency already provided by color:"transparent" + QQuickWindow::setDefaultAlphaBuffer(true) (verified present)
- **Files modified:** none
- **Verification:** window renders transparent (DWM rounded corners visible in smoke test)
- **Committed in:** a3085b9

---

**Total deviations:** 5 auto-fixed (5 Rule 1 won't-work)
**Impact on plan:** All deviations were correctness fixes for plan-snippet issues; the plan's architecture (singleton + static shadow + dual ParallelAnimation shell + reject/re-close lifecycle) was preserved exactly.

## Issues Encountered
- **Key injection debugging:** PostMessage WM_KEYDOWN with VK_ESCAPE translates to Qt::Key_unknown unless a scan code is provided in lParam; Qt 6.11 reorganized the Key enum (Key_Escape == 0x01000000, verified in qnamespace.h). Real input (WScript.Shell AppActivate + SendKeys) is the reliable path for key tests.
- **Windows close refire:** A rejected close on Windows keeps being retried by the OS; the closing-flag pattern (first reject, second accept) was discovered through instrumented runs (temporary qInstallMessageHandler file log + window eventFilter — removed after diagnosis).
- **WIN32 stderr:** QML errors are invisible for GUI-subsystem apps; the temporary file-logging message handler pattern is the go-to diagnostic and worth keeping in the toolbox for future phases.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Theme singleton + shell are consumable by plan 03 (tests/tst_shell.cpp QCOMPARE on Theme tokens, window contract) and Phase 3 (search UI consumes Theme.* only)
- Plan 03 inherits the instrumentation patterns (message handler file log) for its FrameTimeProbe
- Known follow-up: closeAnim/Qt.quit interplay documented above — plan 03's window-contract test must exercise the closing flag pattern, not a naive close

---
*Phase: 01-core-shell*
*Completed: 2026-08-09*
