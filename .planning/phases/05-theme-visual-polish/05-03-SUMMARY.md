---
phase: 05-theme-visual-polish
plan: 3
subsystem: core-settings
tags: [qt6, qsettings, ini, qcolor, qobject, qml-notify, qttest, accent-system]

# Dependency graph
requires:
  - phase: 04-file-search
    provides: LaunchHistory makeSettings factory (LaunchHistory.cpp:24-30, copied verbatim) + sync-after-write discipline + QSettings value-member pattern
  - phase: 05-02
    provides: CMakeLists.txt file-ownership serialization (depends_on contract; no code dependency)
provides:
  - "SettingsStore QObject store: Q_PROPERTY(QColor accent NOTIFY accentChanged), Q_INVOKABLE setAccent, live-read accent(), makeSettings factory verbatim, INI key theme/accent — the single accent owner (D-13/D-14)"
  - "tst_settings suite (6 cases): missingKeyDefaultsTo0078D4, corruptValueFallsBack, setReadRoundTrip, accentChangedEmitted, persistsAcrossInstances, invalidSetIgnored — all through a REAL temp INI (QTemporaryDir seam) — ctest 16/16 overall"
affects: [theme-visual-polish (plan 05-05: context property settingsStore wired in main.cpp before loadFromModule; MainWindow Connections onAccentChanged feeds Theme.accent), phase 6 (accent picker calls setAccent; autostart toggle reuses the store)]

# Tech tracking
tech-stack:
  added: [none — Qt Core/Gui only (QSettings, QColor, QObject); in-tree]
  patterns: ["makeSettings factory copied VERBATIM from LaunchHistory.cpp:24-30 (IniFormat + UserScope TID/wisp; explicit path = test seam)", "QSettings value member via factory prvalue (guaranteed elision; C2280/C2280-free — LaunchHistory Phase-04 lesson)", "Silent fallback (D-16): value() default + QColor::fromString validity gate — missing/corrupt/unparseable ALL return #0078D4, no warnings, no toasts", "sync() after EVERY write (LaunchHistory.cpp:48/58 discipline)", "QObject + Q_PROPERTY NOTIFY for QML Connections wiring (research Pattern 4 — the 05-05 Connections { target: settingsStore; function onAccentChanged() } REQUIRES the signal)", "UI-thread-only contract documented in header (Pitfall 7) — no mutex (unlike LaunchHistory WR-01); QSettings not touched from workers"]

key-files:
  created: [src/core/SettingsStore.h, src/core/SettingsStore.cpp, tests/tst_settings.cpp]
  modified: [CMakeLists.txt]

key-decisions:
  - "SettingsStore derives QObject (D-14 planner's call) — the QML Connections onAccentChanged wiring in 05-05 REQUIRES the NOTIFY signal; LaunchHistory's plain-class shape is NOT copied, only the makeSettings factory + QSettings discipline"
  - "accent() is a LIVE read (readAccent() on every call) — no cached member, no staleness after external INI edits; QML consumes once at startup + via signal"
  - "setAccent stores c.name() — canonical #rrggbb, HexArgb #aarrggbb when alpha < 255 — full fidelity for Phase 6's picker; round-trips through QColor::fromString"
  - "Contract for 05-04/05-05 locked in the plan frontmatter: SettingsStore(const QString &settingsPath = {}, QObject *parent = nullptr); QColor accent() const (+NOTIFY); Q_INVOKABLE void setAccent(const QColor&); context property name 'settingsStore'; INI key theme/accent"
  - "REQUIREMENTS.md VISU-02 stays Pending until phase completion (LAUN-02 precedent); SUMMARY frontmatter lists it per template"

patterns-established:
  - "QSettings store shape: value member constructed via anonymous-namespace factory (prvalue, guaranteed elision) — move-less class by design"
  - "D-16 silent fallback chain: raw string from value(key, default) → QColor::fromString → isValid() ? c : default"
  - "Q_INVOKABLE write path: validity gate → setValue → sync() → emit (persist, THEN notify — the Phase-6 picker path is test-proven before the UI exists)"

requirements-completed: [VISU-02]

# Metrics
duration: 14min
completed: 2026-08-10
---

# Phase 05 Plan 03: SettingsStore — Accent Single Source of Truth Summary

**SettingsStore (D-13/D-14): the QObject accent store over the shared wisp INI — Q_PROPERTY(QColor accent NOTIFY accentChanged) + Q_INVOKABLE setAccent with the LaunchHistory makeSettings factory copied verbatim, D-16 silent #0078D4 fallback for missing/corrupt/unparseable values, sync-after-every-write discipline, and the notify path Phase 6's picker will drive — proven by a 6-case QtTest suite through real temp INIs (ctest 16/16).**

## Performance

- **Duration:** ~14 min
- **Started:** 2026-08-10T19:39:23Z (session start)
- **Completed:** 2026-08-10T19:44:46Z (last task commit)
- **Tasks:** 2
- **Files modified:** 4 (3 created, 1 modified)

## Accomplishments

- **SettingsStore (src/core)**: QObject store (D-14 planner's call — the QML `Connections { target: settingsStore; function onAccentChanged() }` wiring in 05-05 REQUIRES the NOTIFY signal, per research Pattern 4). `Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)`, `explicit SettingsStore(const QString &settingsPath = {}, QObject *parent = nullptr)` (empty → default UserScope TID/wisp INI; non-empty → tst QTemporaryDir seam), `QColor accent() const` (live read via readAccent()), `Q_INVOKABLE void setAccent(const QColor &c)` (Phase-6 picker entry point).
- **makeSettings factory copied VERBATIM** from LaunchHistory.cpp:24-30 into an anonymous namespace — same INI (`%APPDATA%\TID\wisp\wisp.ini`), non-colliding key `theme/accent`; QSettings value member constructed via the factory prvalue in the member-init list (C2280-safe, LaunchHistory Phase-04 lesson).
- **D-16 silent fallback**: `value("theme/accent", "#0078D4")` → `QColor::fromString` → `isValid() ? c : QColor("#0078D4")` — missing key, corrupt string, and unparseable color ALL silently return the default (no warnings, no toasts). setAccent stores `c.name()` (canonical #rrggbb / HexArgb #aarrggbb when alpha < 255 — full Phase-6 fidelity), then `sync()` (LaunchHistory.cpp:48/58 discipline), then `emit accentChanged(c)`.
- **Threading contract documented in the header** (Pitfall 7): UI-thread-only — no mutex now (unlike LaunchHistory's WR-01 QMutex); the header says to add the LaunchHistory QMutex discipline if a worker ever needs it.
- **tst_settings**: 6 green cases (8 PASS incl. init/cleanup) — missingKeyDefaultsTo0078D4 (fresh INI → #0078D4), corruptValueFallsBack (pre-writes "not-a-color" via a raw QSettings seed BEFORE constructing the store → #0078D4), setReadRoundTrip (#E81123 set→read), accentChangedEmitted (QSignalSpy count 1 + arg is the new QColor), persistsAcrossInstances (store2 on the same iniPath sees #107C10 — value survived to disk), invalidSetIgnored (QColor() → accent unchanged AND spy.count() == 0). Every case goes through a REAL temp INI — never %APPDATA%.
- **CMake wiring**: `src/core/SettingsStore.cpp` added to the wisp_core source list (LNK2019 rule); `tst_settings` target + `add_test` + `ENVIRONMENT_MODIFICATION` entry. Full build 46/46 targets link clean; full regression ctest **16/16 passing** (15 existing + tst_settings 0.16s).

## Task Status

| # | Task | Commits | Status |
|---|------|---------|--------|
| 1 | SettingsStore.h + SettingsStore.cpp — QSettings INI accent store | `c9addfe` | Done |
| 2 | tst_settings.cpp + CMakeLists.txt wiring | `39fd0d2` | Done |

## Verification

- Plan greppable gates ✓: `class SettingsStore : public QObject` + `Q_PROPERTY(QColor accent` + `NOTIFY accentChanged` + `Q_INVOKABLE void setAccent` in SettingsStore.h; `theme/accent`, `QSettings(QSettings::IniFormat, QSettings::UserScope` (factory verbatim), `QColor(QStringLiteral("#0078D4"))` (D-16 fallback), `sync()`, `emit accentChanged` in SettingsStore.cpp; `QTEST_MAIN(TstSettings)`, `QTemporaryDir`, `missingKeyDefaultsTo0078D4`, `corruptValueFallsBack`, `persistsAcrossInstances`, `accentChangedEmitted`, `QSignalSpy` in tst_settings.cpp; `SettingsStore.cpp` in CMakeLists.txt wisp_core list.
- `cmake --build --preset dev` — full build clean (46/46 targets); `ctest --test-dir build/dev` — **16/16 passing**.
- Standalone `cl` syntax check of SettingsStore.cpp (task-1 verify, pre-wiring) with `/Zc:__cplusplus` — compiled clean (05-02 precedent).

## Files Created/Modified

- `src/core/SettingsStore.h` — QObject store contract: accent Q_PROPERTY + NOTIFY, Q_INVOKABLE setAccent, threading contract (UI-thread-only, Pitfall 7), D-16 fallback semantics
- `src/core/SettingsStore.cpp` — verbatim makeSettings factory (anonymous namespace), readAccent D-16 gate, setAccent persist→sync→emit, live-read accent()
- `tests/tst_settings.cpp` — 6-case suite (tst_history skeleton shape) via QTemporaryDir seam
- `CMakeLists.txt` — wisp_core source + tst_settings target + add_test + ENVIRONMENT_MODIFICATION

## Decisions Made

- QObject base (D-14 planner's call) — the research Pattern 4 Connections wiring REQUIRES the NOTIFY signal; only the makeSettings factory + QSettings discipline are copied from LaunchHistory, not the plain-class shape.
- `accent()` is a live read — always fresh after external edits; the QML binding consumes once at startup + via signal (no staleness, no cache invalidation design).
- setAccent persists `c.name()` then syncs then emits — persist BEFORE notify so the QML re-read in onAccentChanged always sees the persisted value.
- No mutex: UI-thread-only contract documented instead (T-05-12 accepted — LaunchHistory already serializes its own access; SettingsStore writes only from the picker path on the UI thread).
- Invalid colors silently ignored in setAccent (validity gate first) — no-op, no notify (tested by invalidSetIgnored).
- VISU-02 stays Pending in REQUIREMENTS.md until phase close (LAUN-02 precedent) — listed in frontmatter per template only.

## Deviations from Plan

### Auto-fixed Issues

**1. [Gate/counting — 05-01/05-02 precedent] `rg -c "tst_settings"` counts 5, plan expects 3**
- **Found during:** task 2 (CMakeLists wiring)
- **Issue:** The plan gate expects exactly 3 `tst_settings` occurrences, but valid CMake normal form produces 5: `qt_add_executable(tst_settings …)`, `target_link_libraries(tst_settings …)`, `add_test(NAME tst_settings COMMAND tst_settings)` (2), and the `set_tests_properties` ENVIRONMENT_MODIFICATION entry. Same discrepancy 05-01 (4 vs 3) and 05-02 (5 vs 3) hit and documented.
- **Fix:** Kept idiomatic CMake (5 occurrences); gate deviation documented rather than contorting the build file — per the established precedent.
- **Files modified:** CMakeLists.txt
- **Verification:** ctest runs tst_settings (registered, green, in the env-modified list)
- **Committed in:** 39fd0d2 (task 2 commit)

---

**Total deviations:** 1 (gate/counting — no code deviations)
**Impact on plan:** None — the deviation is purely the documented count discrepancy between the plan's grep expectation and idiomatic CMake. All behavior gates (target builds, test runs, 100% pass) are met.

## Issues Encountered

1. **`rg` not on PATH in this shell** — the plan's acceptance greps were executed with the equivalent project grep tool instead (same patterns, verified matches). Environment, not code (05-02 precedent).
2. **Standalone compile check needed /Zc:__cplusplus** — the task-1 verify (`cmake --build build/dev --target wisp_core`) is a no-op until task 2 wires SettingsStore.cpp into wisp_core; used a direct `cl` syntax check with the Qt module include dirs (QtCore + QtGui) + `/Zc:__cplusplus` (Qt requirement), compiled clean, then proven again by the real full build in task 2 (05-02 precedent).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **Contract for 05-04/05-05** (locked in this plan's frontmatter key_links): context property `settingsStore` registered BEFORE `loadFromModule` (main.cpp:96-98 pattern); MainWindow does `Component.onCompleted: Theme.accent = settingsStore.accent` + `Connections { target: settingsStore; function onAccentChanged() { Theme.accent = settingsStore.accent } }`; Theme.qml's only structural change is `accent` losing readonly (D-15 derived variants bind).
- Threat model T-05-10 (corrupt/malicious value) mitigated: QColor::fromString validity gate + c.name() canonical storage — tested by corruptValueFallsBack. T-05-11 (huge string DoS) accepted: bounded by INI size, fromString fails fast. T-05-12 (concurrency) accepted: UI-thread-only contract documented. T-05-13 (info disclosure) accepted: no secrets in the INI.
- No blockers or concerns outstanding.

## Self-Check: PASSED

- FOUND: `src/core/SettingsStore.h`, `src/core/SettingsStore.cpp`, `tests/tst_settings.cpp` (created)
- Commits verified in `git log`: `c9addfe` (feat), `39fd0d2` (test)

---
*Phase: 05-theme-visual-polish*
*Completed: 2026-08-10*
