---
phase: 04-file-search
plan: 03
subsystem: core-launch-policy
tags: [qt, qsettings, ini, shell-execute, launch-history, explorer-reveal]

# Dependency graph
requires:
  - phase: 04-file-search
    plan: 01
    provides: AppEntry Source::File + isFolder semantics (file rows, displayName/targetPath shape)
  - phase: 04-file-search
    plan: 02
    provides: FileSearch coordinator (the D-06 second search source that consumes trackedExecutables)
  - phase: 03-app-search-result-model-app-catalog
    provides: LaunchController D-12/D-13 policy seams (ResultReporter/Launcher/DismissHandler), WinLaunch firewall
provides:
  - LaunchHistory: {path -> count} launch-tracking store + never-pruned manual-add store in the existing wisp INI (D-10/D-11), native-separator key normalization
  - WinLaunch::revealInExplorer: Explorer reveal with quoted, native-normalized /select argument (LAUN-03)
  - LaunchController phase-4 policy: D-05 silent-normal elevation mapping for file/folder rows, revealSelected() with D-12 freeze + D-13 dismiss + launchFailed classification, launch recording in the default reporter
affects: [04-file-search plan 05 (wiring: setHistory/setRevealer + Ctrl+Enter key), phase 5, v2 recency (LAUN-07/08)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "QSettings factory returning by value (guaranteed elision) — QSettings is non-copyable/non-movable, so a ternary member-init fails (C2280); HotkeyManager's pointer analog does not apply to a value member"
    - "QSettings group iteration via allKeys() prefix filtering (no group-list API) with slash-free group names + native-separator path keys"

key-files:
  created:
    - src/core/LaunchHistory.h
    - src/core/LaunchHistory.cpp
    - tests/tst_history.cpp
  modified:
    - src/win/WinLaunch.h
    - src/win/WinLaunch.cpp
    - src/core/LaunchController.h
    - src/core/LaunchController.cpp
    - tests/tst_launch.cpp
    - CMakeLists.txt

key-decisions:
  - "LaunchHistory::m_settings is a value member constructed via makeSettings() factory — the plan's ternary member-init cannot compile on MSVC (QSettings copy ctor deleted, C2280); returning a prvalue uses guaranteed elision"
  - "recordLaunch counts via launchCount(path)+1 (path-based accessor, not key-passed) — the key double-normalization would be idempotent but path-based reads cleaner"
  - "Comment rewording in LaunchController.cpp to keep the literal 'runas' string out of the file — the D-05 mapping provably never uses the elevation verb (effectiveElevated=false for Source::File), and the grep gate demanded zero occurrences"

patterns-established:
  - "D-05 elevation mapping: effectiveElevated = (source == File) ? false : elevated, computed in launchEntry BEFORE the launcher call — the launcher never sees an elevated file row"
  - "Reveal policy mirrors the ResultReporter table exactly: Launched -> dismiss, CancelledByUser -> quiet, Failed -> launchFailed(displayName)"

requirements-completed: [LAUN-02, LAUN-03]

# Metrics
duration: 6min
completed: 2026-08-10
---

# Phase 4 Plan 3: Launch Semantics (Tracking + Reveal + D-05 Policy) Summary

**LaunchHistory INI store (D-10/D-11) + WinLaunch::revealInExplorer (LAUN-03) + LaunchController phase-4 policy — D-05 silent-normal for file/folder rows, D-12-freeze revealSelected with D-13 dismissal, and launch recording in the default reporter — proven by 7 history suites and 9 new launch-policy suites, all against injected fakes and a real temp-INI round-trip.**

## Performance

- **Duration:** 6 min (04:44:01Z → 04:49:53Z UTC, git timestamps)
- **Started:** 2026-08-10T04:44:01Z (first commit)
- **Completed:** 2026-08-10T04:49:53Z (last code commit)
- **Tasks:** 2 (both TDD: RED test commit + GREEN implementation commit each)
- **Files modified:** 9 (3 created, 6 modified)

## Accomplishments

- **D-10 launch tracking:** every successful executable launch (file rows and classic apps) increments `launchHistory/<path>` in `%APPDATA%\TID\wisp\wisp.ini` via QSettings IniFormat (HotkeyManager pattern); UWP rows skipped by the empty-targetPath guard; `sync()` after every write.
- **D-11 manual store:** `addedExecutables/<path>` group is the distinct, never-pruned store; `trackedExecutables()` unions both groups deduped by path (added wins), returning Source::File entries with filename-derived displayName — the D-06 second search source for FileSearch.
- **Key safety:** native-separator normalization (`QDir::toNativeSeparators`) means '/' can never parse as a QSettings group separator — guarded by the forward-slash suite; displayName is never stored (derived only, grep-verified).
- **LAUN-03 reveal:** `WinLaunch::revealInExplorer` runs `explorer.exe /select,"<path>"` via ShellExecuteExW (open verb, FLAG_NO_UI, no process handle) — quoted and native-normalized so spaces/leading slashes can't break the argument (T-04-07 mitigated; no shell involved).
- **D-05 silent-normal:** `launchEntry` computes `effectiveElevated = false` for Source::File rows before the launcher call — Ctrl+Shift+Enter on files/folders launches normal with zero signals, zero dismissal, no runas verb (grep-verified zero occurrences of the string in LaunchController.cpp).
- **revealSelected():** D-12 snapshot freeze at keypress, Source::File gate before the revealer (T-04-09 — Lnk/Uwp rows structurally cannot reach explorer.exe), Launched → dismiss (D-13), CancelledByUser → quiet, Failed → launchFailed(displayName).
- **TDD gates satisfied:** RED commits (`test(04-03)`) confirmed by failing builds (C1083 missing header / C2039 missing members) before each GREEN (`feat(04-03)`).

## task Commits

Each task was committed atomically:

1. **task 1: LaunchHistory store + tst_history** - RED `2dd5f92` (test) → GREEN `cb180bb` (feat)
2. **task 2: WinLaunch::revealInExplorer + controller policy + tst_launch** - RED `524defd` (test) → GREEN `2028da7` (feat)

**Plan metadata:** pending docs commit

_Note: both tasks are TDD — RED test commit and GREEN implementation commit are separate._

## Files Created/Modified

- `src/core/LaunchHistory.h` - contract per plan interfaces: recordLaunch/addExecutable/trackedExecutables/launchCount, QSettings value member, default %APPDATA%\TID\wisp\wisp.ini or explicit test-seam path
- `src/core/LaunchHistory.cpp` - makeSettings() factory (guaranteed elision — see Deviations), prefix-filtered allKeys() group iteration, union-dedupe (added first, then count desc), native-separator keyFor/normalize
- `tests/tst_history.cpp` - seven suites on a QTemporaryDir ini: round-trip reload (D-10), Source::File shape, manual persist (D-11), union dedupe, forward-slash normalization, UWP skip, name-derivation
- `src/win/WinLaunch.h` - revealInExplorer declaration with LAUN-03/T-04-07 doc contract
- `src/win/WinLaunch.cpp` - revealInExplorer implementation beside launchClassic (same LaunchResult classification, wide-buffer discipline)
- `src/core/LaunchController.h` - setHistory/setRevealer seams, Revealer alias, Q_INVOKABLE revealSelected(), m_history/m_revealer members
- `src/core/LaunchController.cpp` - D-05 effectiveElevated mapping, recordLaunch in the default Launched branch, default revealer bridge, revealSelected() with D-12/D-13 classification
- `tests/tst_launch.cpp` - fileEntry()/folderEntry() helpers + nine new suites (16 total: 7 existing all still green)
- `CMakeLists.txt` - LaunchHistory.cpp in wisp_core; tst_history target in BUILD_TESTING

## Decisions Made

- **makeSettings() factory instead of ternary member-init** — the plan's exact constructor expression cannot compile on MSVC: `cond ? QSettings(...) : QSettings(...)` requires the deleted copy/move ctor of QSettings (C2280). Returning a prvalue from a helper uses guaranteed copy elision (C++17); the value member keeps the class move-less/destructor-less as the contract demands.
- **recordLaunch reads the count via `launchCount(path)`** rather than passing the composed key — keeps the path-based accessor as the single counting point (idempotent with the key variant, cleaner contract).
- **Comment wording keeps 'runas' out of LaunchController.cpp entirely** — the grep gate required zero occurrences; the mapping never used the verb (test-proven), so only doc comments were reworded to "elevation verb".

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] QSettings ternary member-init fails to compile (C2280)**
- **Found during:** task 1 (GREEN; first build after writing LaunchHistory.cpp)
- **Issue:** the plan's constructor `m_settings(settingsPath.isEmpty() ? QSettings(...) : QSettings(...))` fails on MSVC — the conditional operator needs copy/move construction of a class operand and QSettings' copy ctor is explicitly deleted. (HotkeyManager's identical-looking pattern works because it stores a *pointer*.)
- **Fix:** hoisted a `makeSettings()` factory in the anonymous namespace returning QSettings by value (guaranteed elision); member-init is `m_settings(makeSettings(settingsPath))`. No behavior change — same two construction paths.
- **Files modified:** src/core/LaunchHistory.cpp
- **Verification:** build.ps1 clean; tst_history 7/7 green.
- **Committed in:** cb180bb (task 1 GREEN)

**2. [Rule 1 - Gate compliance] 'runas' grep gate tripped by doc comments**
- **Found during:** task 2 (acceptance gate `grep -n "runas" src/core/LaunchController.cpp` must return nothing)
- **Issue:** three comment lines referenced the elevation verb ("no runas variant/verb") — two inherited from the 03-04 D-11 comments, one new in the D-05 block. The code path itself never used it (effectiveElevated=false for File rows is test-proven), but the literal gate demanded zero occurrences.
- **Fix:** reworded all three comments to "elevation verb"; no code change.
- **Files modified:** src/core/LaunchController.cpp
- **Verification:** grep count 0; full rebuild clean; tst_launch 16/16 green.
- **Committed in:** 2028da7 (task 2 GREEN)

---

**Total deviations:** 2 auto-fixed (1 blocking compile fix, 1 gate-compliance reword).
**Impact on plan:** Both fixes were required to build and to pass the plan's own acceptance gates; zero behavior change and zero scope creep.

## Issues Encountered

- **Git commit message parsing failure (process, not product):** the first GREEN commit attempt for task 2 failed because the message contained a double-quote-inside-argument sequence (`/select,\"path\"`) that PowerShell mis-parsed mid-command. Re-issued with the quote removed from the message body — no code impact, no partial commit (the failed attempt staged nothing).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 04-05 wiring can call `launchController.setHistory(&history)` (real LaunchHistory default-path instance), `setRevealer` is optional (default = WinLaunch bridge), and QML's shell Keys block gains Ctrl+Enter → `launchController.revealSelected()` per PATTERNS §6.
- FileSearch's D-06 tracked-source can consume `LaunchHistory::trackedExecutables()` as the second search source (04-02 contract — injectable `std::function<QVector<AppEntry>()>`).
- v2 recency (LAUN-07/08) consumes `launchCount()` / the launchHistory group; the addedExecutables group is documented as the never-pruned store.
- Threat register: T-04-07 (reveal argument injection) mitigated by quoting + native normalization + no-shell ShellExecuteExW; T-04-09 (reveal on non-files) mitigated by the Source::File gate — both test-proven.

## Self-Check: PASSED

- [x] `src/core/LaunchHistory.h` + `src/core/LaunchHistory.cpp` — recordLaunch/addExecutable/trackedExecutables/launchCount, group constants, toNativeSeparators, sync() after writes (setValue|sync count 4 ≥ 3; setValue.*displayName == 0)
- [x] `tests/tst_history.cpp` — 7 declared slots; ctest green
- [x] `src/win/WinLaunch.{h,cpp}` — revealInExplorer with `/select,"`, toNativeSeparators ≥ 1, FLAG_NO_UI, CancelledByUser classification
- [x] `src/core/LaunchController.{h,cpp}` — setHistory/setRevealer/revealSelected/effectiveElevated/recordLaunch (count 5 ≥ 3); `runas` occurrences == 0
- [x] `tests/tst_launch.cpp` — 16 declared slots (7 existing + 9 new); ctest green
- [x] `CMakeLists.txt` — LaunchHistory.cpp in wisp_core; tst_history in BUILD_TESTING
- [x] TDD gate: RED `2dd5f92`/`524defd` (test) precede GREEN `cb180bb`/`2028da7` (feat) in git log
- [x] `build.ps1` clean; `ctest --test-dir build/dev` full suite 13/13 green

---
*Phase: 04-file-search*
*Completed: 2026-08-10*
