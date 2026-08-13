---
phase: 03-app-search-result-model-app-catalog
plan: 04
subsystem: core
tags: [qt6, shellexecuteex, runas, com, uwp, iapplicationactivationmanager, qtest]

# Dependency graph
requires:
  - phase: 03-app-search-result-model-app-catalog (03-01)
    provides: AppEntry contract (source/targetPath/arguments/aumid) + ResultsModel::snapshotSelected (D-12 freeze seed)
  - phase: 03-app-search-result-model-app-catalog (03-03)
    provides: AppCatalog → setEntries pipeline; source-tagged entries (Lnk vs Uwp) drive the policy branches
  - phase: 02-global-hotkey-toggle
    provides: LauncherController::hideNow() contract (D-13 dismissal target, wired in 03-05 via the dismiss-handler seam)
provides:
  - WinLaunch firewall: launchClassic (ShellExecuteEx open/runas on resolved .lnk target, args + target-parent lpDirectory, quiet UAC-cancel) + launchUwp (IApplicationActivationManager::ActivateApplication by AUMID, HRESULT-safe)
  - LaunchController policy: launchSelected/launchIndex with D-12 snapshot freeze, D-11 source-aware elevation refusal + quiet UAC, D-13 instant dismissal via injectable handler
  - tst_launch (7 behaviors): freeze, UWP refusal, elevated classic + dismiss, quiet cancel, failure signal, launchIndex path, null-model safety
affects: [03-05-result-list-ui, phase-05 icons + highlight, phase-04-file-search]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "ResultReporter classification seam: Launcher (void, records WHAT was launched) + ResultReporter (outcome → signals/dismiss policy) — controller-owned policy, OS-free testability (PATTERNS §2 DI)"
    - "ShellExecuteEx quiet-error discipline: SEE_MASK_FLAG_NO_UI + explicit ERROR_CANCELLED/SE_ERR_ACCESSDENIED → CancelledByUser classification; the classifier decides all failure UX (D-11)"
    - "Local GUID constant for CLSID_ApplicationActivationManager (value verified against the 10.0.26100.0 ShObjIdl_core.h DECLSPEC_UUID) — self-contained TU, no uuid.lib linkage"
    - "SEE_MASK_NOCLOSEPROCESS handle closed without wait — D-13 instant path; STACK's 'wait on handle' applies to the legacy no-handle path only"

key-files:
  created: [src/win/WinLaunch.h, src/win/WinLaunch.cpp, src/core/LaunchController.h, src/core/LaunchController.cpp, tests/tst_launch.cpp]
  modified: [CMakeLists.txt]

key-decisions:
  - "Launcher injectable signature carries the ResultReporter param (plan-authorized header adjustment per the Task-2 design note): controller passes its reporter at call time so outcome classification stays controller-owned; tests drive outcomes via the reporter instead of the OS"
  - "SEE_MASK_FLAG_NO_UI on ShellExecuteEx: the controller classifies every failure outcome; UAC-cancel maps to CancelledByUser (quiet, zero signals) via ERROR_CANCELLED(1223)/SE_ERR_ACCESSDENIED(5) — D-11 no error-spam"
  - "CLSID_ApplicationActivationManager embedded as a local GUID constant (45BA127D-10A8-46EA-8AB7-56EA9078943C, read from the SDK header) instead of the EXTERN_C symbol — avoids uuid.lib linkage questions; IID via __uuidof (MIDL_INTERFACE attr)"
  - "lpParameters passed only when arguments non-empty (empty → nullptr); hProcess from SEE_MASK_NOCLOSEPROCESS closed immediately, never waited on (D-13)"

patterns-established:
  - "Launch firewall lifecycle: COM objects created per call on the calling thread; the UI-thread keypress path relies on Qt's existing OLE STA apartment (CoInitializeEx assumed per plan); tests never reach WinLaunch (fake-injected)"
  - "WinLaunch completes the src/win firewall family (WinHotkey/WinFullscreenGuard/WinStartMenuEnumerator/WinUwpEnumerator/WinLaunch) — pure C++ entry points, QML-free, QProcess-free (STACK)"

requirements-completed: [LAUN-01, LAUN-04]

# Metrics
duration: 40min
completed: 2026-08-10
---

# Phase 3 Plan 4: WinLaunch + LaunchController Summary

**ShellExecuteEx runas/activation launch layer with the D-11..D-13 policy proven machine-side: WinLaunch firewall (open/runas with resolved-target + args + target-parent lpDirectory, quiet UAC-cancel; UWP via IApplicationActivationManager by AUMID) behind a LaunchController whose D-12 keypress snapshot freeze, D-11 elevation refusal + silent cancel, and D-13 instant dismissal are all proven by tst_launch 7/7 — full suite 10/10 green, zero OS calls in tests.**

## Performance

- **Duration:** 40 min
- **Started:** 2026-08-10T21:24:00Z (approx; context load + prep)
- **Completed:** 2026-08-10T01:03:58Z
- **Tasks:** 2 (1 auto + 1 auto TDD: RED → GREEN)
- **Files modified:** 6 (5 created, 1 modified)

## Accomplishments

- **`WinLaunch` firewall** — `launchClassic`: ShellExecuteExW on the RESOLVED .lnk target (AppEntry from the 03-02 enumerator), `runas` verb for Ctrl+Shift+Enter per STACK (never QProcess), `lpParameters` carries `GetArguments` (RESEARCH §1 — elevation keeps shortcut args), `lpDirectory` = target's parent dir via `QDir::toNativeSeparators` (PITFALLS #13), `SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI`, handle closed with no wait (D-13); `ERROR_CANCELLED`/`SE_ERR_ACCESSDENIED` → `CancelledByUser` (quiet, zero signals, no dialog — D-11). `launchUwp`: `CoCreateInstance(CLSID_ApplicationActivationManager)` → `ActivateApplication(aumid, AO_NONE)` — local GUID constant verified against the 10.0.26100.0 header, HRESULT failures → `Failed` with qWarning, never a crash (RESEARCH unknowns).
- **`LaunchController` policy (D-11..D-13)** — `launchSelected()`/`launchIndex()` take the **D-12 value-copy snapshot at keypress** (`ResultsModel::snapshotSelected()`); elevated UWP refuses **before any attempt** with `adminRequestRefused(displayName)` (03-05's transient hint); `CancelledByUser` is a silent no-op (launcher stays open); `Launched` invokes the injectable dismiss handler (03-05 wires `LauncherController::hideNow()`, D-13); `Failed` → `launchFailed(displayName)` with no dismissal. All policy branches testable via the injectable `Launcher`/`ResultReporter`/`setDismissHandler` seams — **no real apps, no real UAC prompts in CI**.
- **tst_launch 7/7**: freeze (incl. mid-call selection shift + post-call model replacement), UWP refusal (launcher never called, no dismiss), elevated classic (flag forwarded + dismiss once), quiet cancel (zero signals, zero dismissals), failure signal, launchIndex mouse path (row-independent + flag), null-model safety.
- Full suite **10/10 via ctest** — tst_launcher (02 hidden-behavior suite) untouched and green; LauncherController was not modified (additive-only, per plan).
- Grep gate: **no QProcess** in any WinLaunch/LaunchController file (only a doc comment stating the prohibition).

## Task Commits

Each task was committed atomically (TDD task as test → feat cycle):

1. **Task 1: WinLaunch firewall — ShellExecuteEx open/runas + UWP ActivateApplication** - `9623aeb` (feat)
2. **Task 2 RED: LaunchController policy suite** - `2106e03` (test — fails C1083: `core/LaunchController.h` missing)
3. **Task 2 GREEN: LaunchController policy implementation** - `8313e65` (feat)

**Plan metadata:** final docs commit (this SUMMARY + STATE.md + ROADMAP.md + REQUIREMENTS.md)

## Files Created/Modified

- `src/win/WinLaunch.h` - Firewall interface: `launchClassic(entry, elevated)` / `launchUwp(entry)` returning `LaunchResult {Launched, CancelledByUser, Failed}`; COM-per-call + apartment note; no QML/QProcess
- `src/win/WinLaunch.cpp` - ShellExecuteExW open/runas with args + target-parent lpDirectory + FLAG_NO_UI; ERROR_CANCELLED/SE_ERR_ACCESSDENIED → CancelledByUser; ActivateApplication path with local CLSID constant; per-call CoCreateInstance/Release
- `src/core/LaunchController.h` - QObject controller: `setModel` (QPointer, null-safe), `setLauncher`, `setResultReporter`, `setDismissHandler`, `launchSelected(elevated)`, `launchIndex(index, elevated)`, `adminRequestRefused`, `launchFailed`; Launcher/ResultReporter typedefs documented
- `src/core/LaunchController.cpp` - D-12 snapshot at keypress; D-11 UWP refusal before any attempt; default launcher = WinLaunch bridge by source; default reporter = dismiss/silent/launchFailed mapping; default dismiss no-op
- `tests/tst_launch.cpp` - QtTest suite: 7 policy behaviors with injected counting launcher/reporter/dismiss fakes; QTEST_MAIN
- `CMakeLists.txt` - wisp_core sources += WinLaunch.cpp + LaunchController.cpp; `ole32` PRIVATE (CoCreateInstance); tst_launch target in BUILD_TESTING (tst_hotkey pattern)

## Decisions Made

- **Outcome classification is controller-owned**: the plan's Task-2 design note sanctioned the header adjustment — `Launcher` stays a void callback (records WHAT was launched) but receives the controller's `ResultReporter` at call time, so the default bridge classifies WinLaunch results through the same policy path tests exercise. No OS-callable code in any test.
- **Quiet UAC discipline**: `ERROR_CANCELLED`(1223) and `SE_ERR_ACCESSDENIED`(5) map to `CancelledByUser` → zero signals, zero dismissal. Only genuinely unexpected ShellExecuteEx failures produce `launchFailed` (qWarning logged, no UI).
- **Self-contained COM**: `CLSID_ApplicationActivationManager` is declared `EXTERN_C` in ShObjIdl_core.h but its value lives in uuid.lib — embedded the verified GUID (45BA127D-10A8-46EA-8AB7-56EA9078943C, read from the 10.0.26100.0 DECLSPEC_UUID) as a local constant; interface IID via `__uuidof`.
- **Hold exactly one `SEE_MASK_NOCLOSEPROCESS` handle semantics**: close immediately, never wait — the D-13 fast-dismiss path (STACK's "wait on the handle" was for the legacy path; a long wait would slow dismissal).

## Deviations from Plan

### Plan-Authorized Adjustments

**1. [Plan design note — Task 2 header adjustment] Launcher signature extended with the ResultReporter parameter**
- **Found during:** Task 2 RED design (writing the test suite)
- **Issue:** The interfaces block's `void(const AppEntry&, bool)` launcher cannot communicate the three-way outcome (Launched/CancelledByUser/Failed) back to the controller — outcome tests (dismiss-on-success, quiet-cancel, failure-signal) would have been vacuous with a recording-only fake
- **Fix:** Kept `Launcher` a void callback per the plan's design note but added the `ResultReporter` parameter (controller passes its reporter at call time); default launcher bridges WinLaunch and forwards outcomes; default reporter owns the signals + dismiss mapping. All locked exports preserved (`launchSelected`, `launchIndex`, `setLauncher`, `setDismissHandler`, both signals)
- **Files modified:** src/core/LaunchController.h (typedef + setter), tests/tst_launch.cpp
- **Verification:** tst_launch 7/7 with real outcome-driven assertions; full suite 10/10
- **Committed in:** 2106e03 (RED) + 8313e65 (GREEN)
- **Note:** explicitly permitted by the plan ("Adjust the header accordingly (document the deviation in the SUMMARY if you do)")

**2. [Implementation detail] `ole32` linked proactively + CLSID embedded locally**
- **Found during:** Task 1 (link-line prep)
- **Issue:** `CoCreateInstance`/`Release` live in ole32; `CLSID_ApplicationActivationManager`'s GUID value lives in uuid.lib (EXTERN_C decl in the header only)
- **Fix:** Added `ole32` to wisp_core PRIVATE link libs up front (plan allowed adding it if the build failed to resolve); embedded the GUID as a local constant verified against the SDK header's DECLSPEC_UUID
- **Files modified:** CMakeLists.txt, src/win/WinLaunch.cpp
- **Verification:** clean build, no uuid.lib needed
- **Committed in:** 9623aeb

---

**Total deviations:** 2 (1 plan-authorized signature adjustment, 1 implementation-detail choice)
**Impact on plan:** Signature adjustment was the plan's own recommended path for testability; the link/CLSID choices eliminate two potential link failures. No scope creep, no contract loss.

## Issues Encountered

- None — the plan's injection seams plus the QTest conventions from 03-01/03-02 (named-test style, QStringLiteral fixtures, no braced-init-list inside QCOMPARE) carried the whole suite first-try. The only surprise was confirming the SDK header declares the CLSID as EXTERN_C (uuid.lib territory) rather than an inline uuid-annotated type — resolved via the local constant.

## TDD Gate Compliance (Task 2, tdd="true")

- **RED:** `2106e03` — the 7-behavior suite failed with C1083 (`core/LaunchController.h` missing): a genuine missing-implementation failure, not a vacuous pass
- **GREEN:** `8313e65` — minimal policy implementation; tst_launch 7/7 pass; full suite 10/10
- Commit order in git: `test(03-04)` (2106e03) precedes `feat(03-04)` GREEN (8313e65). Task 1's `feat(03-04)` (9623aeb) precedes the test commit by plan design — the tests reference `WinLaunch::LaunchResult`, so the firewall had to exist to compile; the controller policy itself followed strict RED → GREEN.
- No REFACTOR commit needed (GREEN was already minimal and clean).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **03-05 (result-list UI, wave 5)** consumes: `LaunchController` context property with `launchSelected(false/true)` on Enter / Ctrl+Shift+Enter and `launchIndex(row, false)` for mouse clicks; `adminRequestRefused(displayName)` → transient "Only desktop apps can run as administrator" hint (RESEARCH §7 copy contract); `setDismissHandler(controller.hideNow)` wiring (D-13); model wiring via `setModel` (D-12 snapshot source)
- **Codebase map note:** the plan's `key_links` (LaunchController.cpp → WinLaunch.h via `launchClassic|launchUwp`, → ResultsModel.h via `snapshotSelected`, → LauncherController.h via `hideNow` in 03-05 main.cpp) all resolve — the first two exist now, the last lands in 03-05
- **Manual-table items for 03-VALIDATION.md (not CI-asserted, per plan §verification.5):** real UAC prompt + cancel, real elevated launch, real UWP activation via AUMID
- Full phase status: 03-01..03-04 complete; 03-05 (QML result list + wiring) remains in wave 5

---
*Phase: 03-app-search-result-model-app-catalog*
*Completed: 2026-08-10*

## Self-Check: PASSED

- 6/6 created/modified files found on disk: WinLaunch.h, WinLaunch.cpp, LaunchController.h, LaunchController.cpp, tests/tst_launch.cpp, 03-04-SUMMARY.md
- 3/3 task commits verified in git log: 9623aeb (feat), 2106e03 (test RED), 8313e65 (feat GREEN); TDD gate order holds (test before feat)
- Final verification run: `build.ps1 -Config dev` clean; `ctest --test-dir build/dev --output-on-failure` → 10/10 suites pass (incl. new tst_launch + untouched tst_launcher)
- QProcess grep gate: no usage in any WinLaunch/LaunchController file (doc comment only)
- No unexpected tracked-file deletions in any 03-04 commit (git diff --diff-filter=D: empty)