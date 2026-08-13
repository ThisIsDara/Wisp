---
phase: 03
slug: app-search-result-model-app-catalog
status: complete
researched: 2026-08-09
---

# Phase 3 Research — App Search (Result Model + App Catalog)

> Phase-scoped research for "the first vertical slice": fuzzy app search (LAUN-01), keyboard navigation + mouse launch (LAUN-05), run-as-admin launch (LAUN-04). Supplements `research/STACK.md`, `research/ARCHITECTURE.md`, `research/PITFALLS.md` with the catalog/launch facts and interface contracts this phase locks. Verified against Microsoft Learn + Qt 6.11 docs on 2026-08-09.

## Verified Facts (confidence HIGH unless noted)

### 1. Start Menu .lnk enumeration (classic apps)
- **Folders to scan:** `SHGetKnownFolderPath(FOLDERID_Programs)` (per-user, `%APPDATA%\Microsoft\Windows\Start Menu\Programs`) AND `SHGetKnownFolderPath(FOLDERID_CommonPrograms)` (all-users, `%ProgramData%\...`) — recursing into nested subfolders. Skipping `FOLDERID_CommonPrograms` misses machine-wide installs (PowerToys' Program plugin scans both; STACK locked this at HIGH).
- **Shortcut parse:** For each `*.lnk` found: `CoCreateInstance(CLSID_ShellLink)` → `IShellLinkW` → `IPersistFile::Load(wszPath, STGM_READ)` → `GetPath(szPath, ..., SLGP_UNCPRIORITY)` → `GetDescription` (fall back to the .lnk filename without extension as display name) → `GetIconLocation` (icon location stored on the entry now, **icons loaded in Phase 5**).
- **Broken links:** a .lnk whose `Load` or `GetPath` fails (target deleted) must be **skipped, not crash** — per-link try/catch, log, continue. PITFALLS #4 "Looks Done But Isn't" requires a clean golden list (Calculator/Terminal/Notepad), not 100% coverage.
- **Arguments:** `GetArguments` must be read and carried for elevation (03-04 `lpParameters`), otherwise `runas` on a shortcut with args (e.g. Terminal with a profile) launches without them. STACK "Elevated launch" contract: resolve target first, then `ShellExecuteEx` with `runas` + `lpParameters`.

### 2. UWP/Store enumeration (C++/WinRT)
- **API shape (locked):** `Windows::Management::Deployment::PackageManager::FindPackagesForUser(L"")` — the **empty SID string = current user** (STACK HIGH; PowerToys uses the exact same strategy). Do **not** enumerate `%ProgramFiles%\WindowsApps` directly (ACL-locked, PITFALLS #4) and do **not** scrape registry Appx keys.
- **Package → apps:** iterate `package.Applications()`; each `PackageApplication` has `AppId` (from AppxManifest `<Application Id>`). **AUMID = `package.Id().PackageFamilyName() + L"!" + app.AppId()`** — this is the launch key, never guess it.
- **Junk filter (mandatory):** skip packages where `IsFramework() == true`; skip applications whose `AppListEntry() == Windows::ApplicationModel::Core::AppListEntry::None` (non-launchable, PITFALLS #4); skip empty `DisplayInfo().DisplayName()`. This is the "no junk entries" acceptance criterion.
- **Display info:** `app.GetAppListEntries()` → `AppListEntry::DisplayInfo()` → `DisplayName()`, `GetLogo(Size)` returns a `RandomAccessStreamReference` — **store the logo ref/location string on the entry only; icon extraction is Phase 5**.
- **C++/WinRT from CMake without NuGet (verified 2026-08-09):** the Windows SDK ships the C++/WinRT projection headers at `%WindowsSdkDir%Include\<WindowsTargetPlatformVersion>\cppwinrt` (Microsoft Learn "SDK support for C++/WinRT" — headers "inside the folder `%WindowsSdkDir%Include<WindowsTargetPlatformVersion>\cppwinrt\winrt`"; include dir = the `cppwinrt` folder, then code does `#include <winrt/Windows.Management.Deployment.h>` etc.). CMake wiring:
  ```cmake
  file(GLOB _sdk_cppwinrt "$ENV{WindowsSdkDir}Include/*/cppwinrt")
  list(SORT _sdk_cppwinrt ORDER DESCENDING)
  target_include_directories(wisp_core SYSTEM PRIVATE "${_sdk_cppwinrt[0]}")
  ```
  (build must run from a vcvars64 shell so `WindowsSdkDir`/`WindowsSDKVersion` are set — build.ps1 already guarantees this). NuGet `Microsoft.Windows.CppWinRT` is optional pinning; SDK headers suffice for this phase. **COM apartment:** enumeration runs on the catalog worker thread → `winrt::init_apartment(winrt::apartment_type::multi_threaded)` at thread start, `winrt::uninit_apartment()` at thread end (or `CoInitializeEx(COINIT_MULTITHREADED)` + `CoUninitialize` — pick one and use it everywhere in the worker). Never create/use the WinRT objects on a different thread than the one that initialized COM (PITFALLS #3 — COM apartment violations cause intermittent hangs).
- **Launch UWP:** `CoCreateInstance(CLSID_ApplicationActivationManager)` → `IApplicationActivationManager::ActivateApplication(aumid, nullptr, AO_NONE, &pid)` (STACK HIGH). **No runas verb exists for UWP** — admin request must be refused at the controller layer (D-11), not attempted.

### 3. Fuzzy matcher (in-house, D-04..D-07)
- **Locked priority ladder:** exact match > name prefix > word-boundary start > any subsequence; case-insensitive; camelCase bonus (uppercase letter after lowercase = boundary, e.g. `NotePad`); `MatchRange { start, length }` per matched character run **returned from day one** (LAUN-06 contract in ROADMAP design note + D-07). No score cutoff (D-06): every subsequence match scores; 1-char queries return everything containing the char. Alphabetical tie-break (D-05) — stable, no recency boosts (v2).
- Golden-list bar (unit tests): `cal`→Calculator, `term`→Terminal, `note`→Notepad must rank at/near top.
- Reference: PowerToys `StringMatcher` scoring rules (word-start bonus, contiguity, spread penalty) — ARCHITECTURE.md §Ranking. ~150 lines pure C++, no Qt deps beyond QString.

### 4. Catalog threading & lifecycle (D-08/D-09)
- **Build:** worker thread at startup behind the resident shell (QtConcurrent/QThread — ARCHITECTURE threading rules; established tool in this repo). **Never in the WM_HOTKEY path** (PITFALLS #14: synchronous enumeration = 1-5s first-open delay; first show must stay <100ms from the hotkey path).
- **Refresh:** each hotkey show checks build age; **rebuild only if older than ~10 minutes** (`ensureFresh()`); rebuild runs on the worker again; **silent swap** — the old catalog stays queryable while the new one builds, atomically swapped on completion (D-08 "silent swap while open").
- **Storage:** in-memory only, no SQLite (D-09; STACK "App index storage" analysis: sub-100ms enumeration needs no persistence).
- **Dedupe (D-10):** on case-insensitive display-name collision between a .lnk entry and a UWP entry → prefer the classic .lnk entry, suppress the UWP duplicate. Exact full-name equality only. UWP-only apps remain.

### 5. Launch & admin (LAUN-04, D-11..D-13)
- **Classic app launch:** `ShellExecuteEx` with `lpVerb = L"open"` on the **resolved .lnk target** (or the .lnk path itself — target resolution lets elevation pass `lpParameters`; STACK's elevated-launch path resolves the target first). `lpDirectory` from the target's parent dir (PITFALLS #13 — some apps misbehave with the launcher's cwd).
- **Elevated launch (Ctrl+Shift+Enter):** `ShellExecuteEx` with `lpVerb = L"runas"`, `SEE_MASK_NOCLOSEPROCESS`, wait on the returned process handle. **`QProcess` cannot elevate** (STACK "What NOT to Use"). User-cancelled UAC → process handle closed immediately / `SE_ERR_ACCESSDENIED` → **do not error-spam** (D-11); treat as a normal no-op.
- **UWP admin refusal (D-11):** UWP entries refuse admin at the controller layer; the shell shows a **transient status hint "Only desktop apps can run as administrator"** (never a modal).
- **Selection freeze on Enter (D-12):** launch targets snapshotted at keypress — a result shift mid-launch can't launch the wrong app.
- **Launch dismissal (D-13):** instant `hideNow()` path (Phase-2 D-02.4, proven by tst_launcher) — no animation wait. Consumed via `LauncherController::hideNow()` which already exists.

### 6. Results model & QML (LAUN-05, D-01..D-03)
- **Full list on empty query (D-01):** empty query shows the complete installed-app list, alphabetical (case-insensitive), from the catalog — no cap (virtual scroll handles overflow; 640×400 shell shows ~7 of 44px rows).
- **First row selected by default (D-02); mouse hover also selects.** Selection state lives in the model (Qt model/view contract — ARCHITECTURE §2: `QAbstractListModel` roles + selection API) — matching PowerToys Run feel.
- **Nav keys (LAUN-05):** ↑/↓, PageUp/PageDown, Home/End navigate; Enter launches; mouse click launches. Keys handled at the shell Item level (Phase-1/2 pattern: keyboard focus on an Item, never the Window).
- **UI tokens are locked** in `qml/Theme.qml` + `01-UI-SPEC.md` (approved contract whose Phase Boundary explicitly covers Phase 3's search bar/result list/empty states): rows exactly 44px (`Theme.rowHeight`), 4px grid, selected row = accent `#0078D4` 100% bg + white title (4.7:1), **hover = `surfaceSecondary` `#2D2D30`, never accent**, title 15/400, subtitle 12/400 `textSecondary`, keycap 12/600, placeholder **"Type to search apps and files…"**, empty-query state **"Recent apps will appear here"**, no-match state **"No results for \"{query}\""**, body **"Press Esc to close"**. **No icon pipeline this phase** (Phase 5) — monogram/letter placeholder in the row is acceptable (CONTEXT.md discretion).

### 7. Copy contract (from 01-UI-SPEC Copywriting Contract — Phase 3+ columns, now in scope)
| Element | Copy |
|---|---|
| Search placeholder | "Type to search apps and files…" |
| Empty-query heading | "Recent apps will appear here" |
| No-match heading | "No results for \"{query}\"" |
| Empty-state body | "Press Esc to close" |
| Admin refusal hint (D-11) | "Only desktop apps can run as administrator" |

## Validation Architecture

- **Framework:** Qt Test (Qt6::Test) — already wired (tst_shell/hotkey/launcher/capture/tray + CTest, all green). New targets this phase: `tst_matcher` (plan 01), `tst_model` (plan 01), `tst_enum` (plan 02), `tst_catalog` (plan 03), `tst_launch` (plan 04).
- **Mapping (plan → what's proven):**
  - `tst_matcher` (03-01): golden list (`cal`→Calculator, `term`→Terminal, `note`→Notepad), priority ladder (exact>prefix>boundary>subsequence), camelCase bonuses, case-insensitivity, no-cutoff (1-char query), alphabetical tie-break, **match ranges exactness** (positions/lengths per char run — the LAUN-06 contract).
  - `tst_model` (03-01): empty query → full alphabetical list (D-01), filter-vs-catalog correctness, selection index + moveSelection bounds (↑/↓/PageUp/PageDown/Home/End), snapshotEntry() freeze (D-12 seed).
  - `tst_enum` (03-02): pure helpers — AUMID builder (`family + "!" + appId`), display-name fallback, junk-filter decision fn (framework/AplistEntry=none/empty-name predicates) with fixture data; the live COM walks are dev-machine smoke (manual table) + catalog integration.
  - `tst_catalog` (03-03): injected scanner fakes — dedupe precedence (.lnk wins over UWP on exact case-insensitive name, D-10), alphabetical sort (D-03), age-based refresh logic (10-min threshold), silent-swap consistency (entries() returns the same snapshot while a rebuild is in flight).
  - `tst_launch` (03-04): controller launch policy with injected launcher fn — snapshot freeze (D-12), UWP+admin → refusal signal (D-11), cancelled-UAC no-spam (single quiet return), success → `hideNow()` called (D-13), existing tst_launcher suite stays green (additive-only controller change).
- **Manual-only verifications (03-VALIDATION.md):** live scan of the dev machine's real Start Menu + Store apps (golden list present, no junk, no dupes); real app launch from the compiled launcher; real UAC prompt + cancel path; UWP app launch via AUMID; Ctrl+Shift+Enter elevated launch of an admin-needing app. GUI/COM-environment tests only (needs a live desktop session).
- **Quick run:** `ctest --test-dir build/dev --output-on-failure` (~5s). Full: `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure`.
- **Windows note:** UWP tests that call `PackageManager` need a real session — keep them in the dev-machine smoke, not CI. Enumeration tests must not assert on machine-specific app lists (golden list asserted in tst_matcher with fixture names, not live scans).

## Decisions This Phase Imposes (locked, mirror into plans)

1. **D-01:** Empty query → full installed-app list, alphabetical (case-insensitive), no cap — computed by the same catalog → model.
2. **D-02:** First row selected by default on show; mouse hover also selects.
3. **D-03:** Plain alphabetical column order on empty query (dupes broken by D-10).
4. **D-04:** Priority ladder exact > prefix > word-boundary > subsequence, case-insensitive, camelCase bonuses; golden list `cal`/`term`/`note`.
5. **D-05:** Alphabetical tie-break (no catalog-order/image-name bonuses; LAUN-07 recency is v2).
6. **D-06:** No score cutoff — every subsequence match scores; UI-thread filter <5ms.
7. **D-07:** Matcher is a pure in-house C++ function returning **score + match ranges** from day one; unit-tested against the golden list.
8. **D-08:** Catalog builds on a worker thread at startup; each show checks age, rebuilds only if >10min; silent swap while open.
9. **D-09:** In-memory only, no SQLite.
10. **D-10:** On case-insensitive display-name collision .lnk wins, UWP suppressed (exact equality only).
11. **D-11:** Classic launch via `ShellExecuteEx`; Ctrl+Shift+Enter = `runas` + `SEE_MASK_NOCLOSEPROCESS`; cancelled UAC not error-spammed; UWP refuses admin with transient status hint "Only desktop apps can run as administrator".
12. **D-12:** Selection freezes on Enter — snapshot at keypress.
13. **D-13:** Launch dismissal uses existing `hideNow()` instant path.

## Remaining Unknowns (accepted, not blockers)
- Exact per-machine Store-app availability differs (Win10 vs Win11 app sets) — golden list asserted with fixture names in unit tests; live-store coverage is dev-machine manual verification.
- `ActivateApplication` on a Store app that is suspended/not installed for the user returns an HRESULT — handle as a normal launch failure, never a crash.
- .lnk files with UWP bridge targets (`shell:AppsFolder` reparse-style shortcuts): `IShellLink::GetPath` returns the underlying alias; elevation of those is a no-op refusal path — treat bridge .lnk like UWP for admin (refuse gracefully) if target resolution indicates a Store app, else attempt normally.