---
phase: 06-tray-settings-autostart-packaging
plan: 06-04
subsystem: boot-wiring
tags: [main-cpp, single-instance, named-event, autostart, tray, settings-window, accent, boot-order, cpp]

# Dependency graph
requires:
  - phase: 06-tray-settings-autostart-packaging (06-01)
    provides: WinSingleInstance (tryAcquire/signalShow/startWatching/showRequested) + AutostartManager (isEnabled/setEnabled)
  - phase: 06-tray-settings-autostart-packaging (06-02)
    provides: SettingsWindow.qml + ColorDialog.qml surfaces (injected via the shared engine)
  - phase: 06-tray-settings-autostart-packaging (06-03)
    provides: TrayIcon settingsRequested/setAccent + SettingsWindow controller (injected ctor)
  - phase: 02-global-hotkey-toggle / 05-theme-visual-polish
    provides: LauncherController::showUserRequested, SettingsStore::accentChanged/accent(), HotkeyManager, HotkeyCaptureDialog, tray.show() → connect → hotkeys.start() boot order
provides:
  - Boot wiring in src/app/main.cpp: single-instance guard FIRST (before window/tray construction) with silent second-instance exit after signalShow; exact-match --autostart parse (D-12 consumption, T-06-07); showRequested → LauncherController::showUserRequested summon (D-09); tray Settings → SettingsWindow::open (D-04); accent → tray disc at startup + accentChanged live repaint (UI-SPEC tray contract)
  - The app is now fully resident: single instance, quiet autostart, tray-only settings entry
affects: [06-05]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Guard-first boot: WinSingleInstance constructed + tryAcquire() at the very top of main() — before QApplication/window/tray construction (plain QObject + pure Win32 calls need no app instance); duplicate → signalShow() + return 0 (silent, no UI surface)"
    - "Named-event summon: connect showRequested → showUserRequested BEFORE startWatching — a pre-watch signal stays pending (Pitfall 1) and fires once the watcher runs; explicit-intent showUserRequested bypasses the fullscreen guard (deliberate second launch, same as tray Open wisp)"
    - "Wiring-home discipline (PATTERNS anti-pattern 1): main.cpp owns every cross-object connect — tray.setAccent(settingsStore.accent()) + accentChanged lambda; TrayIcon never touches the store"
    - "Constructor injection: SettingsWindow(engine, settingsStore, autostart, hotkeys, capture, parent) — all collaborators passed, no service lookup"

key-files:
  created: []
  modified:
    - src/app/main.cpp

key-decisions:
  - "The plan's 'suppress the initial launcher window show' premise does not exist in the shipped app: the launcher boots resident-hidden by design (D-02.1, MainWindow.qml visible:false; user chose 'Boot to tray, window hidden' over 'Show window at boot' in the discussion log). The --autostart parse is implemented as the D-12 consumption contract (exact-string match, T-06-07) with Q_UNUSED; D-11's quiet posture holds for both boot paths by construction — no behavior change"
  - "showRequested connect + startWatching placed after controller.setFullscreenGuard and BEFORE TrayIcon construction — outside the tray-availability branch so the second-instance summon works in the tray-less fallback too (the connect is in scope for both branches)"
  - "tray.setAccent(settingsStore.accent()) called right after TrayIcon construction (pre-show): setAccent just repaints the icon; the shown tray picks up the icon at show()"

patterns-established:
  - "Guard-first single-instance boot (SYS-01): tryAcquire before any window/tray construction; duplicate → signalShow + silent exit"
  - "Summon wiring: connect-then-watch ordering (no show request can be missed; Pitfall-1 pending event covers the connect-watch gap)"
  - "Cross-object wiring concentrated in main.cpp (tray↔settings, store↔tray); controllers stay collaborator-injected and store-free"

requirements-completed: [SYS-01, SYS-02, SYS-03]

# Metrics
duration: 9min
completed: 2026-08-11
---

# Phase 06 Plan 04: Resident Boot Wiring Summary

**The app becomes fully resident: single-instance guard FIRST in main() (duplicate process signals the running instance via the named-event channel and exits silently — no UI surface), exact-match `--autostart` parse (D-12 consumption, T-06-07), second-instance `showRequested` → launcher summon (D-09), tray Settings → settings window (D-04, tray-only entry), and accent → tray disc at startup plus live `accentChanged` repaint (UI-SPEC tray contract) — 19/19 tests green and the second-instance smoke verified: second launch exits 0, first instance survives.**

## Performance

- **Duration:** 9 min
- **Started:** 2026-08-11T21:48Z
- **Completed:** 2026-08-11T21:59Z
- **Tasks:** 2
- **Files modified:** 1 (src/app/main.cpp)

## Accomplishments

- **Single-instance guard FIRST (SYS-01, D-09):** `WinSingleInstance singleInstance;` + `tryAcquire()` are the first statements of `main()` — before QApplication, window, tray, or any other construction (CONTEXT boot order). A duplicate process takes the `signalShow()` path (create-or-open + SetEvent — a pre-watch signal stays pending, RESEARCH Pitfall 1 fix) and `return 0`s silently: no UI surface, no toast (UI-SPEC second-instance contract). QObject + pure Win32 calls are app-instance-free, so the guard runs cleanly pre-QApplication.
- **--autostart parse (D-12/T-06-07):** `QCoreApplication::arguments().contains(QStringLiteral("--autostart"))` — exact-string match, no other argv interpretation, unknown args ignored. The launcher boots resident-hidden by design (D-02.1), so D-11's "quiet to tray, window hidden" posture holds for both boot paths; the parse is the D-12 consumption contract (see deviation 1).
- **Second-instance summon (D-09):** `connect(&singleInstance, &WinSingleInstance::showRequested, &controller, &LauncherController::showUserRequested)` placed after `controller.setFullscreenGuard` and before tray construction — outside the tray branch, so the summon works in the tray-less fallback too. `startWatching()` follows the connect (connect-then-watch: no request can be missed; the watcher thread's signal auto-queues to the GUI thread). Explicit-intent `showUserRequested` bypasses the fullscreen guard — a deliberate second launch is user intent, same as tray "Open wisp".
- **Tray → Settings (SYS-03, D-04):** `AutostartManager autostart;` instantiated with the stores (06-01), then `SettingsWindow settingsWindow(&engine, &settingsStore, &autostart, &hotkeys, &capture, &app)` constructed after the capture dialog (all collaborators injected — 06-03 contract). `connect(&tray, &TrayIcon::settingsRequested, &settingsWindow, &SettingsWindow::open)` in the tray branch. Settings opens from the tray ONLY — no launcher-window affordance added (D-04).
- **Accent → tray disc (UI-SPEC tray contract):** `tray.setAccent(settingsStore.accent())` at startup (same silent-fallback path as the launcher; missing/corrupt value → #0078D4) and `connect(&settingsStore, &SettingsStore::accentChanged, &tray, [&tray](const QColor &c) { tray.setAccent(c); })` — live repaint when the picker changes the accent. The wiring lives in main.cpp; TrayIcon never reaches into the store (PATTERNS anti-pattern 1).
- **Wave gate:** full configure + build (wisp.exe linked with the SettingsWindow controller finally instantiated — the 06-03 controller is now referenced by a binary) + **19/19 ctest green**.
- **Second-instance smoke (automated process-level):** launched wisp twice — first instance (pid 90160) survived, second instance (pid 88300) exited with code 0, exactly one wisp process remained, cleanup left zero. Registry clean: no `wisp` value in the HKCU Run key (smoke does not touch autostart). The visual half — "first instance shows the launcher window" — is a pending manual step (see User Setup Required).

## task Commits

Each task was committed atomically:

1. **task 1: Single-instance guard + --autostart parse + showRequested summon** - `6a6a90d` (feat)
2. **task 2: Tray Settings → settings window + accent → tray disc wiring** - `00c9fef` (feat)

**Plan metadata:** 8c59bab (docs: update tracking after wave 3 — prior commit)

## Files Created/Modified

- `src/app/main.cpp` - Guard-first boot: WinSingleInstance + tryAcquire before QApplication (signalShow + return 0 on duplicate); exact-match `--autostart` parse (D-12/T-06-07); `showRequested` → `showUserRequested` connect + `startWatching()` pre-tray; `AutostartManager` instantiation; `tray.setAccent(settingsStore.accent())` startup call; `SettingsWindow` construction with injected collaborators; tray-branch connects `settingsRequested` → `open` and `accentChanged` → `setAccent` (lambda)

## Decisions Made

- The `--autostart` suppression semantic is satisfied by construction (launcher always boots hidden — D-02.1); the parse exists as the D-12 contract consumption with T-06-07 exact-match discipline (deviation 1 documents the premise mismatch).
- The summon connect + `startWatching()` live outside the tray-availability branch — the second-instance path must work in the tray-less fallback too (the app is resident either way).
- `tray.setAccent(...)` at construction time (pre-show) is safe: `setAccent` only repaints the icon, and the tray picks the icon up at `show()`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Plan premise mismatch] No "initial launcher window show" exists to suppress — the launcher always boots hidden**
- **Found during:** task 1 (--autostart parse)
- **Issue:** The plan assumes an initial window show at boot ("suppress the initial launcher window show", read_first: "find where the launcher window is first shown"). The shipped app has none: MainWindow.qml declares `visible: false` (D-02.1 resident — starts hidden, hotkey summons it), main.cpp never calls show(), and the discussion log's user choice explicitly rejected "Show window at boot" in favor of "Boot to tray, window hidden". D-11's quiet posture already holds for every boot path.
- **Fix:** Implemented the parse exactly as specified (exact-string match, T-06-07) as the D-12 consumption contract, with a comment documenting that the D-11 suppression is satisfied by construction; `Q_UNUSED` keeps it warning-free. No behavior change — the app's resident boot posture is untouched.
- **Files modified:** src/app/main.cpp
- **Verification:** grep gates pass (`--autostart` present); build + 19/19 ctest green; smoke shows no behavioral regression
- **Committed in:** 6a6a90d (task 1 commit)

---

**Total deviations:** 1 auto-fixed (plan premise mismatch, no code-behavior impact)
**Impact on plan:** None — all acceptance gates pass; the D-11/D-12 contract is honored with the shipped boot posture.

## Issues Encountered

- None. Build environment (vcvars64 via build.ps1) worked cleanly; no running wisp.exe held the output file; the smoke left no stray processes and no registry residue.

## User Setup Required

- **Pending manual step (visual):** run the second-instance smoke and confirm the FIRST instance's launcher window appears when a second wisp.exe is launched (D-09 surface). The process-level contract is verified (second exits 0, first survives, single process remains); the on-screen launcher appearance needs eyes. Suggested: `.\run.ps1`, wait 2s, launch `build\dev\wisp.exe` again, observe the launcher pop over, close both via tray Quit.

## Next Phase Readiness

- **06-05 (packaging/LGPL):** the app is fully resident and boot-wired — release packaging can proceed. No new APIs to consume; the runbook's second-launch and autostart steps can now run against the real binary.
- **CMakeLists.txt:** untouched this plan (all wiring is within src/app/main.cpp); 19 tests registered and green.
- No blockers.

## Known Stubs

None. `autostartBoot` is parsed and deliberately unused (documented contract marker — deviation 1); every connect is live-wired; the SettingsWindow controller is now instantiated by the app binary (its 06-03 "not yet referenced" note is resolved).

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: none | — | No new security surface beyond the plan's register: the named-event show channel is the accepted T-06-02 (session-local `Local\` namespace; worst case a session process pops the launcher — nuisance only); the argv parse is exact-string T-06-07 mitigation (no other interpretation, unknown args ignored); the Run-key value (T-06-01) is written only by AutostartManager by construction — main.cpp never touches it. All connects are in-process signals. |

---

*Phase: 06-tray-settings-autostart-packaging*
*Completed: 2026-08-11*

## Self-Check: PASSED

Both task commits verified in git log (6a6a90d, 00c9fef); src/app/main.cpp verified on disk with all acceptance greps passing; 06-04-SUMMARY.md verified on disk; no unintended file deletions in the last commit; wave gate: full build + 19/19 ctest green; second-instance smoke verified at the process level (second exits 0, first survives, single process remains, no registry residue).

