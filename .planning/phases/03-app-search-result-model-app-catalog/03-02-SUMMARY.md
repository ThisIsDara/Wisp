---
phase: 03-app-search-result-model-app-catalog
plan: 02
subsystem: core
tags: [qt6, com, ishelllink, cppwinrt, winrt, packagemanager, uwp, qtest]

# Dependency graph
requires:
  - phase: 03-app-search-result-model-app-catalog (03-01)
    provides: AppEntry struct (Source enum, displayName, targetPath, arguments, aumid, iconRef) + wisp_core stats lib pattern
provides:
  - WinStartMenuEnumerator::scanStartMenu / scanRoots: classic app inventory from FOLDERID_Programs + FOLDERID_CommonPrograms, recursive *.lnk walk via IShellLinkW, broken-link-tolerant
  - WinUwpEnumerator::scanUwpApps / isSkippable / buildAumid / displayNameOr: Store/UWP inventory for the current user via C++/WinRT PackageManager, junk filter (framework / no AppListEntry / empty name)
  - tst_enum (7 behaviors): junk-filter matrix, exact AUMID format, name fallback, broken-link policy, source tagging against a real IShellLinkW fixture
affects: [03-03-app-catalog, 03-04-launch, 03-05-result-list-ui, phase-05 icons]

# Tech tracking
tech-stack:
  added:
    - "C++/WinRT via Windows SDK headers only (10.0.26100.0 cppwinrt include dir, SYSTEM PRIVATE in CMake — no NuGet on this machine)"
    - "windowsapp.lib (PRIVATE link on wisp_core) for RoGetActivationFactory / RoOriginateLanguageException symbols not in winhttp/ole32"
  patterns:
    - "src/win firewall: pure C++ entry points (QString/QVector in, COM/COM+WinRT detail out) — scanRoots(QStringList) file-injection seam for unit tests"
    - "COM apartment reuse: CoInitializeEx with S_FALSE / RPC_E_CHANGED_MODE treated as 'reuse existing apartment', CoUninitialize only when we initialized (COINIT_MULTITHREADED + MTA + OLE STA coexistence verified)"
    - "Broken-link policy: parseLnk returns std::optional<AppEntry>; garbage links are qWarning-skipped, never fatal; per-link try/catch belt-and-braces"
    - "Windows Defender TEMP-fixture hazard: never write MZ-magic junk into QTemporaryDir test fixtures — heuristic writer scanning quarantines them (fixture silently vanishes)"

key-files:
  created: [src/win/WinStartMenuEnumerator.h, src/win/WinStartMenuEnumerator.cpp, src/win/WinUwpEnumerator.h, src/win/WinUwpEnumerator.cpp, tests/tst_enum.cpp]
  modified: [CMakeLists.txt]

key-decisions:
  - "IconRef format 'iconPath;index' fixed now: GetIconLocation returns path + index; Phase 5 splits on the last ';'"
  - "Display-name fallback = .lnk complete base name when GetDescription yields nothing (deterministic in fixture: progmatically-created shortcuts have no description)"
  - "UWP AUMID comes from the OS (AppListEntry.AppUserModelId()) instead of Package.Applications — the 26100 SDK projection has no Package.Applications; buildAumid(PFN,appId) seam kept and unit-tested for format contract, live scan uses the OS value (identical by contract: PFN!AppId)"
  - "No QDir::NoSymLinks anywhere near .lnk enumeration — Qt 6.11 classifies Windows .lnk as links and the filter silently hides every shortcut (production bug caught by tst_enum)"

patterns-established:
  - "WindowsRuntime + C++/WinRT include wiring without NuGet: file(GLOB WindowsSdkDir Include/*/cppwinrt) + list(SORT DESCENDING) + list(GET 0) — plain _sdk_cppwinrt[0] subscript syntax is unsupported by the installed CMake"
  - "QtTest diagnostic routing on this machine: QtTest stdout is swallowed under pwsh pipes AND cmd redirection; use QtTest's own -o <file>,txt which bypasses the console entirely (supersedes 03-01 note about Start-Process)"
  - "windowsapp.lib is required for C++/WinRT-compiled TUs (RoGetActivationFactory); link it PRIVATE on wisp_core once, not per-target"

requirements-completed: [LAUN-01]

# Metrics
duration: 45min
completed: 2026-08-10
---

# Phase 3 Plan 2: Start Menu (.lnk) + UWP/Store app enumerators Summary

**Both Windows app sources behind the `src/win/` firewall: recursive IShellLinkW .lnk scan of per-user + all-users Start Menu folders (broken-link tolerant, icon path + args captured) and a C++/WinRT PackageManager scan with the tested junk filter (framework/no-AppListEntry/empty-name) and PFN!AppId AUMID format, plus a 7-behavior contract suite that caught a real production bug (QDir::NoSymLinks silently hides every .lnk).**

## Performance

- **Duration:** ~45 min
- **Started:** 2026-08-10T00:19:38Z
- **Completed:** 2026-08-10
- **Tasks:** 3 (2 auto + 1 auto TDD)
- **Files modified:** 6 (4 created, 2 modified)

## Accomplishments

- `WinStartMenuEnumerator`: starts from BOTH `FOLDERID_Programs` and `FOLDERID_CommonPrograms` (missing CommonPrograms would skip machine-wide installs), recursive `*.lnk` walk with per-link `IShellLinkW` parse (GetPath/GetArguments/GetDescription/GetIconLocation); garbage or unparseable links are skipped with a log line, never fatal
- `WinUwpEnumerator`: `PackageManager::FindPackagesForUser(L"")` (current user), framework-package skip, `GetAppListEntries()` + OS-provided `AppUserModelId()`, per-package/per-app exception isolation; pure decision helpers `isSkippable` / `buildAumid` / `displayNameOr` exported for unit tests and 03-03's catalog
- `tst_enum`: 7/7 green — junk-filter matrix, exact AUMID format (Calculator fixture), display-name fallback, broken-link policy (garbage .lnk + .txt ignored, exactly 1 valid entry), source tagging with resolved target path
- Full suite 8/8 green via ctest
- TDD task proved its worth before the first commit of that task: the RED run exposed the `NoSymLinks` bug in Task 1's committed code (see deviation #5)

## task Commits

Each task was committed atomically:

1. **task 1: WinStartMenuEnumerator - .lnk walk of both Programs folders** - `c2d468a` (feat)
2. **task 2: WinUwpEnumerator - C++/WinRT PackageManager scan + pure helpers** - `4065a4f` (feat)
3. **task 3: tst_enum - junk filter, AUMID builder, name fallback, broken-link policy** - `65eabd2` (test)

**Task-3 bug fix commit (deviation):** `05af3a6` (fix)

**Plan metadata:** `docs(03-02)` pending (final commit)

## Files Created/Modified

- `src/win/WinStartMenuEnumerator.h` - Header: `scanStartMenu()` / `scanRoots(QStringList)` + `#include <windows.h>` + COM pimpl isolation behind the src/win firewall
- `src/win/WinStartMenuEnumerator.cpp` - FOLDERID resolution, recursive QDirIterator `*.lnk` walk, per-link `parseLnk` (std::optional; qWarning-skip policy), apartment reuse logic
- `src/win/WinUwpEnumerator.h` - Header: `scanUwpApps()`, `isSkippable(bool,bool,bool)`, `buildAumid(pfn,appId)`, `displayNameOr(name,fallback)`
- `src/win/WinUwpEnumerator.cpp` - C++/WinRT PackageManager scan, junk filter, AUMID/name helpers, per-package/per-app try-catch
- `tests/tst_enum.cpp` - 7-behavior contract suite with real IShellLinkW fixture
- `CMakeLists.txt` - wisp_core sources += both enumerators; cppwinrt include dir (SYSTEM PRIVATE); `windowsapp` PRIVATE link; tst_enum registered in BUILD_TESTING

## Decisions Made

- Live UWP AUMID sourced from `AppListEntry.AppUserModelId()` (OS-guaranteed PFN!AppId) rather than manifest scrambling — the 26100 SDK projection lacks `Package.Applications`; the plan's `buildAumid` seam remains as the unit-tested format contract
- C++/WinRT WITHOUT NuGet: Windows SDK headers resolved via `$env{WindowsSdkDir}` (set inside build.ps1's vcvars64) — keeps offline builds working; documented for the manifest/packaging phase
- `CoInitializeEx(COINIT_MULTITHREADED)` coexisting with Qt's OLE STA: `RPC_E_CHANGED_MODE` accepted and work proceeds on the existing apartment (verified via tst_enum running under QGuiApplication's message loop)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] CMake unsupported list subscript syntax**
- **Found during:** task 2 (CMake wiring)
- **Issue:** `target_include_directories(... ${_sdk_cppwinrt[0]})` rejected by the installed CMake (no `$var[idx]` subscript; only `list(GET)` / `$<JOIN>` forms exist)
- **Fix:** `file(GLOB _sdk_cppwinrt "$ENV{WindowsSdkDir}Include/*/cppwinrt")` + `list(SORT ... DESCENDING)` + `list(GET _sdk_cppwinrt 0 ...)` — newest SDK wins deterministically
- **Files modified:** CMakeLists.txt
- **Verification:** configure + build green
- **Committed in:** 4065a4f

**2. [Rule 3 - Blocking] winrt::hstring → QString has no implicit conversion**
- **Found during:** task 2 (compilation)
- **Issue:** `QString() = hstring` fails to compile; `hstring` also lacks an implicit `std::wstring` conversion in this projection
- **Fix:** local `toQString(winrt::hstring)` helper via `fromWCharArray` (empty-string safe)
- **Files modified:** src/win/WinUwpEnumerator.cpp
- **Verification:** build green
- **Committed in:** 4065a4f

**3. [Rule 3 - Blocking] Package.Applications missing from the 26100 SDK projection**
- **Found during:** task 2 (compilation against real headers)
- **Issue:** The plan's `Package.Applications()` / `PackageApplication` path doesn't exist in `winrt::Windows::ApplicationModel::Package` for Windows SDK 10.0.26100 — only `GetAppListEntries()`
- **Fix:** Switched the live scan to `package.GetAppListEntries()` + `entry.AppUserModelId()` (OS-provided AUMID); kept `buildAumid` as the tested format-contract seam
- **Files modified:** src/win/WinUwpEnumerator.cpp (+ declaration in .h unchanged — exports preserved)
- **Verification:** build green; tst_enum::aumidBuilder locks the PFN!AppId format
- **Committed in:** 4065a4f
- **Note:** behavioral assumption changed (documented in header comment) — the AUMID comes from WinRT, not the manifest

**4. [Rule 3 - Blocking] LNK2019: RoGetActivationFactory / RoOriginateLanguageException unresolved**
- **Found during:** task 2 (link)
- **Issue:** C++/WinRT-generated code needs RoGetActivationFactory (RoApi.h / windowsapp.lib) but the project linked only winhttp/ole32 etc.
- **Fix:** `target_link_libraries(wisp_core PRIVATE windowsapp)` verified via dumpbin that both symbols live in windowsapp.lib
- **Files modified:** CMakeLists.txt
- **Verification:** full link green
- **Committed in:** 4065a4f
- **Pattern saved:** phase manifest/03-PATTERNS note — every C++/WinRT consumer needs `windowsapp`

**5. [Rule 1 - Bug] QDir::NoSymLinks silently hides every .lnk file (Qt 6.11, Windows)**
- **Found during:** task 3 (tst_enum RED run)
- **Issue:** Task 1's `QDirIterator` used `QDir::Files | QDir::NoSymLinks`; on Windows Qt classifies `.lnk` as links for that filter → a real Start Menu scan returned ZERO apps. Probe matrix: files-only → 1 entry, +NoSymLinks → 0
- **Fix:** Use `QDir::Files` alone for the `*.lnk` walk; broken-link tolerance unchanged (garbage still skipped by parseLnk)
- **Files modified:** src/win/WinStartMenuEnumerator.cpp
- **Verification:** tst_enum::brokenLinkPolicy + sourceTagging green; full suite 8/8
- **Committed in:** 05af3a6

**6. [Rule 1 - Bug] Windows Defender quarantines MZ-magic fixture files in TEMP**
- **Found during:** task 3 (fixture debugging)
- **Issue:** The garbage `broken.lnk` fixture started with a `MZ\x90\x00` PE signature; Defender's real-time writer heuristic deleted it from the QTemporaryDir during test setup — brokenLinkPolicy then saw 2 files, sometimes 1
- **Fix:** Junk content without PE magic (plain ASCII + 0x7f poison)
- **Files modified:** tests/tst_enum.cpp
- **Verification:** fixture stable across runs; suite green
- **Committed in:** 65eabd2

---

**Total deviations:** 6 auto-fixed (2 Rule 1, 4 Rule 3)
**Impact on plan:** All fixes were required for compilation, correctness, or a stable fixture. No scope creep; exports per plan unchanged.

## Issues Encountered

- **QTest executable output swallowed entirely on this machine** — pwsh pipes AND `cmd /c > file 2>&1` AND `Start-Process -RedirectStandardOutput` all produced empty files; exit code 2 with zero diagnostics. Resolved with QtTest's own `-o <file>,txt` option (bypasses the console channel). 03-01's note is superseded by this simpler recipe.
- **QTemporaryDir fixture mystery** (the vanished broken.lnk) traced to Windows Defender quarantine, not test logic — see deviation #6.

## TDD Gate Compliance (task 3, tdd="true")

- RED state: the test commit (`65eabd2`) was authored and run against the already-committed Task 1/2 implementation — it FAILED (brokenLinkPolicy/sourceTagging: 0 entries) and thereby exposed the NoSymLinks bug; the failure was real, not a vacuous test
- GREEN: `05af3a6` (fix) + full suite 8/8
- Honest caveat: plan structure put implementation (tasks 1-2) before the test (task 3), so the strict RED-first cycle was not assertable via commit order — `test(03-02)` lands after `feat(03-02)` commits in git history. The failing RED run still caught a production-critical bug that would have shipped an empty app list.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 03-03 (catalog) can consume both enumerators: `scanRoots` / `scanStartMenu` / `scanUwpApps` all return `QVector<AppEntry>` matching the 03-01 contract; pure helpers unit-tested for the filter-free composite-root contract
- `windowsapp.lib` + cppwinrt include wiring are already in CMakeLists.txt — the catalog must NOT re-add them
- Threat surface: no new network endpoints or elevation paths; commit 05af3a6 note in source explains the NoSymLinks pitfall for future Windows enumeration code

---
*Phase: 03-app-search-result-model-app-catalog*
*Completed: 2026-08-10*

## Self-Check: PASSED

- Files verified: WinStartMenuEnumerator.h/.cpp, WinUwpEnumerator.h/.cpp, tests/tst_enum.cpp, 03-02-SUMMARY.md
- Commits verified: c2d468a, 4065a4f, 05af3a6, 65eabd2
- No unexpected tracked-file deletions in commits (git diff --diff-filter=D c2d468a~1..HEAD: empty)