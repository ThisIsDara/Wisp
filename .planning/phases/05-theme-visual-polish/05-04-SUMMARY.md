---
phase: 05-theme-visual-polish
plan: 4
subsystem: core-ui-wiring
tags: [qt6, qml, qquickimageprovider, lru, icontheory, uwp, iconkey, resultsmodel, qttest]

# Dependency graph
requires:
  - phase: 05-01
    provides: WinIconExtractor IconKey parseKey()/extract() seams + WinUwpLogo (extraction entry point bound in main.cpp) — provider calls extract(id) on miss
  - phase: 05-02
    provides: IconCache bounded LRU (get/insert/size, QMutex-guarded, cap 500) — the provider's cache-first contract
  - phase: 05-03
    provides: SettingsStore (unused by this plan — 05-05 consumes it) + CMakeLists.txt file-ownership serialization
provides:
  - "IconProvider (src/core): QQuickImageProvider serving image://wispicons/{id} — LRU get -> miss extract via std::function seam -> insert successes only (failures never cached, D-16); downscale to requestedSize or 32x32 (D-01); runs on Qt's dedicated provider thread"
  - "ResultsModel IconKeyRole ('iconKey'): Lnk -> iconRef verbatim ('path;index') or 'path:'+targetPath; File -> 'path:'+targetPath; Uwp -> 'uwp:PFN|appId' iconRef ('' -> monogram)"
  - "WinUwpEnumerator emits iconRef 'uwp:{PFN}|{appId}' (PFN from PackageId.FullName(), appId split from AUMID) — parseKey-compatible, empty halves never emitted"
  - "main.cpp wires IconCache + IconProvider before the engine and registers 'wispicons' via addImageProvider before loadFromModule — provider registered, unused (05-05 renders)"
  - "tst_model iconKeyRole case: Lnk both branches + Uwp + File rows + roleNames contract — ctest 16/16 overall"
affects: [theme-visual-polish (plan 05-05: ResultsRow.qml Image.source = 'image://wispicons/' + encodeURIComponent(iconKey), cache: false), packaging (windeployqt qml scan unchanged)]

# Tech tracking
tech-stack:
  added: [none — QQuickImageProvider (Qt Quick, in-tree), std::function seam; no new deps]
  patterns: ["Provider-thread get-or-extract: requestImage runs on Qt's single provider thread per engine — blocking extraction is safe there, never the UI thread (05-RESEARCH Pattern 1; STACK HIGH rule)", "Firewall seam: IconProvider holds std::function<QImage(QString)> bound to WinIconExtractor::extract in main.cpp — Win32 never enters src/core", "Failure-not-cached: null extract results skip m_cache->insert (D-16) — monogram stays, re-extract on demand", "AUMID split for appId: PFN!AppId contract (buildAumid unit test) yields the manifest appId without new WinRT API surface", "Stack-order lifetime: provider+engine declared so engine destructs first — provider always outlives the engine (Qt requirement)"]

key-files:
  created: [src/core/IconProvider.h, src/core/IconProvider.cpp]
  modified: [src/win/WinUwpEnumerator.cpp, src/core/ResultsModel.h, src/core/ResultsModel.cpp, src/app/main.cpp, tests/tst_model.cpp, CMakeLists.txt]

key-decisions:
  - "IconKeyRole format follows parseKey's exact grammar (WinIconExtractor.h:49-72): Lnk rows pass the enumerator's iconRef verbatim when set (GetIconLocation 'path;index'), else 'path:'+targetPath; File rows 'path:'+targetPath; Uwp rows the 'uwp:PFN|appId' iconRef, '' when absent — the QML monogram covers it (D-04/D-16)"
  - "UWP appId derived by splitting the AUMID at '!' (PFN!AppId contract locked by tst_enum buildAumid) instead of a second WinRT call — OS contract guarantees the format"
  - "IconProvider ctor takes IconCache* + std::function seam — main.cpp binds WinIconExtractor::extract; provider stays pure Qt (firewall discipline)"
  - "requestImage downscales 64px extraction to the requested size or fixed 32x32 (D-01) with SmoothTransformation; null results return a 32x32 null slot"
  - "REQUIREMENTS.md VISU-02 stays Pending until phase completion (LAUN-02 precedent); SUMMARY frontmatter lists it per template"

requirements-completed: [VISU-02]

# Metrics
duration: 17min
completed: 2026-08-10
---

# Phase 05 Plan 04: Icon Pipeline Wire-Up Summary

**End-to-end icon pipeline (D-02): IconProvider mounts image://wispicons/{id} on Qt's provider thread with LRU-first get-or-extract — ResultsModel exposes the iconKey role in parseKey's grammar (Lnk iconRef/'path:' / File 'path:' / Uwp 'uwp:PFN|appId'), WinUwpEnumerator starts emitting parseKey-compatible uwp iconRefs, and main.cpp registers the provider before loadFromModule — full suite 16/16 green, wisp.exe boots clean with the provider registered but unused (05-05 renders).**

## Performance

- **Duration:** ~17 min
- **Started:** 2026-08-10T16:57:00Z (context loading)
- **Completed:** 2026-08-10T17:09:00Z (last task commit, 848f7ff)
- **Tasks:** 3
- **Files modified:** 8 (2 created, 6 modified)

## Accomplishments

- **IconProvider (src/core)**: `QQuickImageProvider` subclass (Image type — `requestImage` returns QImage). Ctor `IconProvider(IconCache *cache, std::function<QImage(const QString &)> extractor)` — the std::function seam keeps Win32 out of src/core; main.cpp binds `&WinIconExtractor::extract`. `requestImage`: LRU `get(id)` → hit returns instantly; miss → `extractor(id)` → only successes `insert` (D-16 failure-not-cached: null extraction is never cached, the monogram stays and re-extraction happens on demand); non-null → `scaled` to the engine's requestedSize or fixed 32×32 (D-01) with KeepAspectRatio + SmoothTransformation; null → 32×32 slot returned with null image (QML monogram fallback, D-04).
- **ResultsModel IconKeyRole**: role added after IsFolderRole; `roleNames()` maps `"iconKey"`. `data()` builds the key per parseKey's grammar: Uwp → `entry.iconRef` (`""` when the enumerator couldn't emit one — monogram fallback); File → `"path:" + entry.targetPath`; Lnk → iconRef verbatim (recognized `'path;index'` format) or `"path:" + entry.targetPath` when the enumerator left it empty.
- **WinUwpEnumerator iconRef emission**: in the AppListEntry loop, `entry.iconRef = "uwp:" + packageFullName + "|" + appId` — PFN via `package.Id().FullName()` (see deviation 1), appId split from the already-read AUMID (PFN!AppId OS contract, buildAumid-locked). Guard: empty PFN or appId leaves iconRef `""` — malformed keys are never emitted (parseKey would reject them anyway; the QML monogram covers the gap).
- **main.cpp wiring**: `IconCache iconCache;` + `IconProvider iconProvider(&iconCache, &WinIconExtractor::extract);` constructed immediately before `QQmlApplicationEngine engine;` — stack order guarantees engine-destroyed-first (provider always outlives the engine, a Qt requirement). `engine.addImageProvider("wispicons", &iconProvider)` placed after engine creation, before `loadFromModule`. No context properties touched; no QML Image added (05-05).
- **tst_model extension**: `iconKeyRole` case — roleNames contract (`"iconKey"`), Uwp with iconRef, Uwp without iconRef (→ `""`), Lnk with iconRef verbatim, Lnk without iconRef (→ `"path:"+path`), and a File row via the suite's generation-stamped `setFileResults` fixture (→ `"path:"+path`); fixtures `lnkEntry`/`uwpEntry` gained an optional iconRef parameter (default `{}` — all 22 existing call sites untouched).
- **CMake**: `src/core/IconProvider.cpp` added to the wisp_core source list only (app-wiring happens in main.cpp, not CMake) — `rg -c "IconProvider.cpp" CMakeLists.txt` == 1. No new test target (tst_model extended in place).

## Task Status

| # | Task | Commits | Status |
|---|------|---------|--------|
| 1 | UWP enumerator iconRef + ResultsModel iconKey role | `a7b9168` | Done |
| 2 | IconProvider (QQuickImageProvider) with LRU-backed get-or-extract | `8b796e4` | Done |
| 3 | main.cpp wiring — provider registration + full regression | `848f7ff` | Done |

## Verification

- Plan greppable gates ✓: `uwp:` in WinUwpEnumerator.cpp (iconRef emission, line 80); `IconKeyRole` in ResultsModel.h; `"iconKey"` in ResultsModel.cpp; `case IconKeyRole` in ResultsModel.cpp; `iconKey` in tst_model.cpp (≥1 case); `class IconProvider` + `QQuickImageProvider` in IconProvider.h; `requestImage` + `m_cache->get` + `m_cache->insert` in IconProvider.cpp; `IconProvider.cpp` ×1 in CMakeLists.txt; `addImageProvider` + `IconProvider iconProvider` + `IconCache` in main.cpp.
- `cmake --build build/dev` — full build clean (all targets incl. wisp, wisp_core, wisp_qml); `ctest --test-dir build/dev --output-on-failure` — **16/16 passing** (16 suites: tst_shell/hotkey/launcher/capture/matcher/model/enum/catalog/launch/tray/search/filesearch/history/icons/iconcache/settings; tst_model 0.13s with the new iconKeyRole case).
- `rg -c "tst_model" CMakeLists.txt` == 5 (qt_add_executable + target_link_libraries + add_test NAME/COMMAND + ENVIRONMENT_MODIFICATION entry) — pre-existing Phase-3 target, no CMake change in this plan; idiomatic-CMake count documented per the 05-01/02/03 precedent.
- Live smoke: `build/dev/wisp.exe` launched, ran 4s (PID 60356 — no early exit, no crash with the provider registered), killed via Stop-Process.
- Case-level totals: tst_model now 21 case slots (17 prior + iconKeyRole in the 05-04 slot list section).

## Files Created/Modified

- `src/core/IconProvider.h` — QQuickImageProvider subclass contract + threading doc comment (provider-thread model, cache:false consumer contract for 05-05)
- `src/core/IconProvider.cpp` — requestImage get-or-extract + downscale + null-slot fallback
- `src/win/WinUwpEnumerator.cpp` — iconRef `"uwp:{PFN}|{appId}"` emission with empty-half guard
- `src/core/ResultsModel.h` — IconKeyRole in the Roles enum
- `src/core/ResultsModel.cpp` — roleNames `"iconKey"` + data() IconKeyRole case
- `src/app/main.cpp` — IconCache/IconProvider construction + addImageProvider registration before loadFromModule
- `tests/tst_model.cpp` — iconKeyRole case + iconRef parameters on lnkEntry/uwpEntry fixtures
- `CMakeLists.txt` — IconProvider.cpp in the wisp_core source list

## Decisions Made

- **iconKey grammar = parseKey grammar (verbatim)**: the role emits exactly what WinIconExtractor::parseKey consumes — Lnk iconRef `'path;index'` when present, else the `path:`-prefixed form; Uwp `'uwp:PFN|appId'`; `''` only when no iconRef exists (monogram fallback). The QML side (05-05) only needs `encodeURIComponent(model.iconKey)`.
- **AUMID split instead of AppListEntry.ApplicationId**: the 26100 projection's AppListEntry carries AppUserModelId (already read for launch); the OS-documented PFN!AppId contract (unit-locked by buildAumid since Phase 3) makes the split exact — no extra WinRT surface, one less API to break on SDK drift.
- **Failures never cached (D-16) by construction**: the provider only calls `insert` after a non-null extract; null keys/images both terminate in the 32×32 null slot path — T-05-15 (unbounded re-request DoS) stays bounded by the LRU cap and sub-10ms re-extraction.
- **std::function seam over direct dependency**: IconProvider has no Windows headers; anything callable-with-(QString)→QImage can back it (testability + firewall discipline).
- VISU-02 stays Pending in REQUIREMENTS.md until phase close (LAUN-02 precedent) — listed in frontmatter per template only.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `PackageId::PackageFullName` absent from the 26100 cppwinrt projection**
- **Found during:** task 1 (WinUwpEnumerator.cpp build)
- **Issue:** The plan's action said "packageFullName from the Package"; the natural `package.Id().PackageFullName()` fails C2039 — `IPackageId` in the 26100 projection exposes `FullName()`, `FamilyName()`, etc. but no `PackageFullName` (the SDK never added the property to this interface).
- **Fix:** `toQString(package.Id().FullName())` — the same package full name (e.g. `Microsoft.WindowsCalculator_8wekyb3d8bbwe`), matching the precedent of 05-01's FindPackage-API discovery.
- **Files modified:** src/win/WinUwpEnumerator.cpp
- **Verification:** wisp_core builds clean; tst_model green
- **Committed in:** a7b9168 (task 1 commit)

**2. [Rule 1 - Bug] `entry.path` is not an AppEntry member — field is `targetPath`**
- **Found during:** task 1 (ResultsModel.cpp build)
- **Issue:** My IconKeyRole case referenced `entry.path` (the plan's shorthand wording); AppEntry.h declares `targetPath`. C2039 at compile.
- **Fix:** `entry.targetPath` in both `"path:"` fallback branches (Lnk empty-iconRef + File).
- **Files modified:** src/core/ResultsModel.cpp
- **Verification:** wisp_core builds clean; tst_model green (the `path:C:\...` assertions lock the corrected branch)
- **Committed in:** a7b9168 (task 1 commit)

---

**Total deviations:** 2 code (1 blocking API-name, 1 member-name bug), zero gate/counting deviations this plan (IconProvider.cpp ×1 counted exactly as the plan's `rg -c` gate demands)
**Impact on plan:** Both fixes are compile-correctness only — no behavioral or API-surface change beyond the plan's design.

## Issues Encountered

1. **Bare `cmake --build build/dev` fails without the vcvars64 environment** (`type_traits: No such file or directory` — INCLUDE env missing). Known project constraint (STATE.md: "always build via build.ps1"); ran all builds inside `cmd /c "vcvars64.bat >nul && cmake --build ..."` — same environment build.ps1 provides, rebuild verified clean. Environment, not code.
2. **`rg` not on PATH** — acceptance greps executed via the equivalent project grep tool (05-02/03 precedent), all patterns verified.
3. **CMakeLists source-list edit triggers AUTOMOC re-run** on the next build; expected Ninja behavior, no action needed.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **Contract for 05-05 (ResultsRow/Theme)**: the row's `Image` must use `source: "image://wispicons/" + encodeURIComponent(model.iconKey)`, `cache: false` (QPixmapCache must never defeat the LRU), `sourceSize: Qt.size(32, 32)` or leave default (provider downscales itself); `onStatusChanged: Image.Ready/Error` drives the D-04 monogram crossfade; monogram stays the placeholder until the image loads or errors (null 32×32 → `Image.Error` → keep monogram).
- **main.cpp seam already bound**: `iconProvider` is a stack object outliving the engine; 05-05 only touches QML + (per 05-03's contract) the settingsStore context property — no further C++ wiring expected.
- Lnk iconRefs arriving as bare `'path;index'` strings validate against parseKey rule 4; `path:`-prefixed forms validate against rule 2; UWP keys against rule 3 — every iconKey emitted by this plan is parseKey-guaranteed valid or `""`.
- Threat model: T-05-14 mitigated (parseKey exact rules, unit-tested); T-05-15 mitigated (LRU cap 500 + failure-not-cached); T-05-16/T-05-17 accepted as planned (single registration point; public package identifiers).
- No blockers or concerns outstanding.

## Self-Check: PASSED

- FOUND: `src/core/IconProvider.h`, `src/core/IconProvider.cpp` (created)
- Commits verified in `git log` (deletion check clean across HEAD~3..HEAD): `a7b9168` (feat), `8b796e4` (feat), `848f7ff` (feat)
- Grep gates re-verified post-commit (see Verification)

---
*Phase: 05-theme-visual-polish*
*Completed: 2026-08-10*