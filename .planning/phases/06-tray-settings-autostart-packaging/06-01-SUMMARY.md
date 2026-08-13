---
phase: 06-tray-settings-autostart-packaging
plan: 01
subsystem: windows-integration
tags: [win32, mutex, named-event, qsettings, registry, single-instance, autostart, cpp, qttest]

# Dependency graph
requires:
  - phase: 05-theme-visual-polish
    provides: SettingsStore store shape + makeSettings QSettings factory discipline (src/win firewall pattern, tst_* Qt-Test sibling pattern)
provides:
  - WinSingleInstance: session-local named-mutex single-instance gate + named-event show-signal channel (tryAcquire/signalShow/startWatching/showRequested)
  - AutostartManager: HKCU Run-key store writing the exact quoted `"<exe>" --autostart` value (isEnabled/setEnabled)
  - tst_singleinstance + tst_autostart unit suites (SYS-01 mutex semantics, SYS-02 Run-key write/remove/state)
  - CMakeLists.txt wiring: 2 new wisp_core sources + 2 new test targets (19-test suite)
affects: [06-02, 06-03, 06-04, 06-05]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "src/win firewall QObject class: HANDLEs as opaque void* members so Win32 headers never leak into the header (grep-verifiable)"
    - "Named auto-reset event show channel: signalShow() create-or-open FIRST then SetEvent — pre-watch signals stay pending and fire when the watcher starts (Pitfall 1 fix)"
    - "QThread-discipline watcher: std::thread + WaitForSingleObject loop, stopWatching = flag + SetEvent wake + join"
    - "makeRunKey factory mirroring LaunchHistory::makeSettings: empty path -> real HKCU Run key, non-empty -> test seam (QSettings NativeFormat IS the registry wrapper)"
    - "Scoped registry test key (HKCU\\Software\\wisp-tests) with whole-tree cleanup from the parent level — no residue"

key-files:
  created:
    - src/win/WinSingleInstance.h
    - src/win/WinSingleInstance.cpp
    - src/core/AutostartManager.h
    - src/core/AutostartManager.cpp
    - tests/tst_singleinstance.cpp
    - tests/tst_autostart.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "D-09 channel: named auto-reset event (Local\\wisp-show-launcher) rather than WM_COPYDATA — no window handle needed, race-safe via create-or-open before SetEvent"
  - "Watcher thread is std::thread (WinHotkey precedent) with flag+SetEvent+join shutdown — never WaitForSingleObject on the GUI thread"
  - "tst_autostart value assertion: exact QCOMPARE against the constructed expected value (strongest D-12 check) + shape regex; the plan's fixed 'wisp.exe' regex would fail on the tst_autostart.exe binary"
  - "Cleanup removes the whole wisp-tests key tree from HKCU\\Software (parent level) — the seam's parent key is created as a QSettings side effect and must not linger"

patterns-established:
  - "Win32-handle firewall: void* members + Windows.h confined to the .cpp; header comment must not even name the header (grep gate)"
  - "makeRunKey QSettings factory (guaranteed elision, non-copyable member)"
  - "Registry test seams: scoped HKCU key + init()/cleanup() removing the whole tree"

requirements-completed: [SYS-01, SYS-02]

# Metrics
duration: 13min
completed: 2026-08-11
---

# Phase 06 Plan 01: Single-Instance + Autostart Foundation Summary

**Win32 single-instance gate (named mutex + named auto-reset event show channel) and HKCU Run-key autostart store writing the exact quoted `"<exe>" --autostart` value, both behind pure interfaces with new Qt Test suites — the tested C++ building blocks plans 06-02..06-05 wire into**

## Performance

- **Duration:** 13 min
- **Started:** 2026-08-11T16:45:20Z
- **Completed:** 2026-08-11T16:58:18Z
- **Tasks:** 3
- **Files modified:** 7 (4 created sources, 2 created tests, 1 modified)

## Accomplishments

- `WinSingleInstance` behind the `src/win/` firewall: `CreateMutexW(Local\wisp-single-instance)` gate (`tryAcquire` → `ERROR_ALREADY_EXISTS` = duplicate), create-or-open named event + `SetEvent` show channel (`signalShow`, Pitfall-1 race-safe), std::thread watcher emitting `showRequested()` (never the GUI thread), flag+SetEvent+join shutdown. Zero Win32 headers in the header (grep-gated).
- `AutostartManager` (UI-thread-only store, SettingsStore discipline): `makeRunKey` factory — empty path → real `HKCU\...\CurrentVersion\Run` via QSettings NativeFormat; non-empty → `tst_autostart` scoped-key seam. `setEnabled(true)` writes the exact D-12 value `"<exe>" --autostart` built from `QCoreApplication::applicationFilePath()` (T-06-01: never user input, quotes by construction); `isEnabled()` = value exists (D-16 silent false); disable removes the value; always `sync()`.
- Test suites: `tst_singleinstance` (same-process duplicate acquisition rejected + show-signal event round-trip via QSignalSpy/QTRY_VERIFY, no Windows.h in tests) and `tst_autostart` (initiallyDisabled / enableWritesQuotedValue with exact-value QCOMPARE + shape regex / disableRemovesValue with disk readback) — registry residue-free (whole-tree cleanup verified on HKCU).
- Wave gate: full `cmake --preset dev` + build + **19/19 ctest green** (17 existing + 2 new).

## task Commits

Each task was committed atomically:

1. **task 1: WinSingleInstance — mutex + show-signal channel behind the src/win firewall** - `2027b0b` (feat)
2. **task 2: AutostartManager — HKCU Run key store with quoted `--autostart` value** - `5bc8f8a` (feat)
3. **task 3: Wave gate — full build + complete ctest suite green** - no commit (verification-only; tasks 1-2's CMake wiring proved coherent as-is)

**Plan metadata:** 64820b8 (docs: create phase plan — prior commit)

## Files Created/Modified

- `src/win/WinSingleInstance.h` - Pure C++ interface: `tryAcquire`/`signalShow`/`startWatching`/`stopWatching`/`showRequested`; HANDLEs as opaque `void*` members; no Win32 headers (firewall rule)
- `src/win/WinSingleInstance.cpp` - `CreateMutexW`/`CreateEventW`/`SetEvent`/`WaitForSingleObject`; `Local\` session namespace; watcher thread + shutdown
- `src/core/AutostartManager.h` - QObject store: `isEnabled`/`setEnabled` with injection-seam ctor; UI-thread-only contract documented
- `src/core/AutostartManager.cpp` - `makeRunKey` factory (empty → real HKCU Run key, else test seam); D-12 quoted value construction; `sync()` after every mutation
- `tests/tst_singleinstance.cpp` - Mutex duplicate-acquisition + event-channel round-trip tests (SYS-01)
- `tests/tst_autostart.cpp` - Run-key initial/enable/disable tests with scoped key + whole-tree cleanup (SYS-02)
- `CMakeLists.txt` - `WinSingleInstance.cpp` + `AutostartManager.cpp` in wisp_core; `tst_singleinstance` + `tst_autostart` targets, registration, and PATH env properties (sibling pattern)

## Decisions Made

- Named auto-reset event (create-or-open before `SetEvent`) chosen as the show channel — race-safe without a window handle (D-09's open discretion, matches RESEARCH).
- Watcher implemented as `std::thread` (WinHotkey precedent) with flag + SetEvent wake + join shutdown; `emit showRequested()` from the worker auto-queues to the owner thread.
- Test value assertion strengthened to exact `QCOMPARE` against the constructed D-12 expected string + shape regex (see deviation 1).
- Cleanup removes the whole `wisp-tests` tree from the `Software` parent level (see deviation 2).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Plan's tst_autostart regex would never match the test binary's path**
- **Found during:** task 2 (AutostartManager)
- **Issue:** The plan specified the stored-value regex `^".*wisp\.exe" --autostart$`, but the value is built from `QCoreApplication::applicationFilePath()` — which in the test is `tst_autostart.exe`, not `wisp.exe` — so the gate could never pass.
- **Fix:** Assert exact equality with the constructed expected string (`"` + appFilePath + `" --autostart`) — the strongest D-12 check — plus a shape regex `^".*\.exe" --autostart$` capturing the plan's intent (quoted path + space + `--autostart`).
- **Files modified:** tests/tst_autostart.cpp
- **Verification:** ctest -R tst_autostart passes; value readback matches both assertions
- **Committed in:** 5bc8f8a (task 2 commit)

**2. [Rule 1 - Bug] Test cleanup left the seam's parent registry key behind**
- **Found during:** task 2 verification (post-build residue check)
- **Issue:** QSettings creates `HKCU\Software\wisp-tests` as a side effect of writing the `Run` seam; cleanup removed only the `Run` subkey, leaving an empty `wisp-tests` key.
- **Fix:** cleanup() now removes the whole `wisp-tests` tree from `HKCU\Software` (parent level, where registry keys map to QSettings groups).
- **Files modified:** tests/tst_autostart.cpp
- **Verification:** Post-test `Test-Path HKCU:\Software\wisp-tests` = False; ctest passes
- **Committed in:** 5bc8f8a (task 2 commit)

**3. [Rule 1 - Bug] Header comment tripped the acceptance grep gate**
- **Found during:** task 1 acceptance checks
- **Issue:** The header's comment literally contained the string "Windows.h", so `grep -c "Windows.h"` returned 2 — the gate requires 0.
- **Fix:** Reworded comments to "Win32 headers" (no literal match).
- **Files modified:** src/win/WinSingleInstance.h
- **Verification:** grep gate now returns 0
- **Committed in:** 2027b0b (task 1 commit)

---

**Total deviations:** 3 auto-fixed (3 Rule 1 bugs in plan spec / my own early code)
**Impact on plan:** All fixes necessary for the plan's acceptance gates to actually pass. No scope creep.

## Issues Encountered

- **wisp.exe file lock blocked the full build (task 3):** `LNK1168: cannot open wisp.exe for writing` — a dev wisp.exe instance (PID 89408, launched earlier from `build/dev`) held the output file. Not a CMake wiring fault (this plan's changes don't touch the app target). Resolved by stopping the stale dev instance; rebuild then succeeded. (Environment issue, not committed.)

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **06-02 (QML surfaces) / 06-03 (tray + settings controller) / 06-04 (main.cpp wiring):** ready to consume `WinSingleInstance::tryAcquire/signalShow/startWatching/showRequested` and `AutostartManager::isEnabled/setEnabled` — both stores are tested and behind the same interfaces the plan contract exports.
- **CMakeLists.txt:** 19 tests registered; new targets follow the sibling pattern exactly.
- No blockers; the only environment note is that a running dev wisp.exe locks the build output (pre-existing behavior).

## Known Stubs

None - both classes are fully implemented, tested, and wired into the build.

---

*Phase: 06-tray-settings-autostart-packaging*
*Completed: 2026-08-11*

## Self-Check: PASSED

All 7 created files verified on disk; both task commits (2027b0b, 5bc8f8a) verified in git log.
