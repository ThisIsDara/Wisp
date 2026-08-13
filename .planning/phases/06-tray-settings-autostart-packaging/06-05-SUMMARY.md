---
phase: 06-tray-settings-autostart-packaging
plan: 06-05
subsystem: packaging
tags: [nsis, installer, lgpl, vc-redist, vc_redist, windows-search, deployment, clean-vm, release-gate]

# Dependency graph
requires:
  - phase: 06-tray-settings-autostart-packaging
    provides: release build + windeployqt deploy output (build/deploy/wisp) from deploy.ps1, hotkey-conflict UI (tray balloon + red labels from 06-02/06-03)
provides:
  - per-user NSIS installer pipeline (installer.nsi + build-installer.ps1) — no UAC, %LOCALAPPDATA%\Programs\wisp, Start Menu shortcut
  - registry-gated silent VC_redist install (D-14) carrying official aka.ms download
  - LGPL compliance evidence pipeline (verify-lgpl.ps1 + relink-test) proving dynamic Qt linkage + re-linkability (D-15)
  - LGPL-COMPLIANCE.md evidence doc + source offer, THIRD-PARTY-NOTICES.txt shipping next to wisp.exe
  - clean-VM validation runbook (VM-RUNBOOK.md) with user-approved PASS results on Win10 22H2 + Win11 24H2 (D-16)
affects: [release/packaging follow-ups, v2 code signing, MSIX packaging]

# Tech tracking
tech-stack:
  added: [NSIS 3.x (winget), makensis, dumpbin (VS2022 MSVC), cl (VS2022), vc_redist.x64.exe bundling]
  patterns: [per-user NSIS install (RequestExecutionLevel user + SetShellVarContext Current + HKCU uninstall keys), VC_redist registry-gated silent exec, LGPL relink test (deploy-only PATH), mechanical compliance verification scripts]

key-files:
  created: [packaging/installer.nsi, packaging/build-installer.ps1, packaging/verify-lgpl.ps1, packaging/relink-test/main.cpp, packaging/LGPL-COMPLIANCE.md, packaging/VM-RUNBOOK.md]
  modified: [packaging/THIRD-PARTY-NOTICES.txt]

key-decisions:
  - "VC_redist carried in the installer and executed only when the HKLM VC-runtime registry check fails (D-14, RESEARCH Open Question 1 resolution)"
  - "Installer OutFile path written relative to installer.nsi (..\\build\\deploy\\wisp-setup.exe) for deterministic output regardless of makensis cwd"
  - "relink test run with deploy-only PATH to prove the DEPLOYED Qt6Core.dll (not dev Qt) satisfies an independently compiled binary — LGPL re-linkability evidence"

patterns-established:
  - "Per-user NSIS install: RequestExecutionLevel user, SetShellVarContext Current in both install and uninstall .onInit, $LocalAppData\\Programs install dir, HKCU uninstall keys"
  - "Mechanical LGPL evidence: dumpbin /DEPENDENTS for dynamic imports + relink binary running against deploy folder with C:\\Qt absent from PATH"
  - "Build gates as PowerShell scripts with $ErrorActionPreference=Stop and explicit exit codes (build-installer.ps1, verify-lgpl.ps1)"

requirements-completed: [SYS-04]

# Metrics
duration: 4d 2h (wave 5 incl. human verification window)
completed: 2026-08-11
---

# Phase 6 Plan 5: Release Gate Summary

**Per-user NSIS installer (no UAC, registry-gated VC_redist) + mechanically verified LGPL compliance evidence (dumpbin imports + deploy-only relink test) + clean-VM runbook user-approved on both Win10 22H2 and Win11 24H2**

## Performance

- **Duration:** 4d 2h (tasks 1–2 + runbook authoring in one session; ~2 days human verification window between checkpoint and user approval)
- **Started:** 2026-08-09
- **Completed:** 2026-08-11
- **Tasks:** 3 (task 3 completed across two executor sessions via human-verify checkpoint)
- **Files modified:** 7 (all in `packaging/`)

## Accomplishments
- Per-user NSIS installer pipeline: `installer.nsi` (RequestExecutionLevel user, `%LOCALAPPDATA%\Programs\wisp`, Start Menu "wisp" shortcut, HKCU uninstall keys, locked UI-SPEC copy) + `build-installer.ps1` (deploy → official VC_redist download → makensis). Build gate green: `build\deploy\wisp-setup.exe` (73.7 MB) exists.
- VC_redist handled per D-14: downloaded from official `aka.ms/vs/17/release/vc_redist.x64.exe`, carried in the installer, executed silently only when the HKLM `VC\Runtimes\x64` Installed check fails (T-06-03 mitigation: official source only + size sanity check).
- LGPL evidence pipeline: `verify-lgpl.ps1` runs dumpbin `/DEPENDENTS` (Qt6Core/Gui/Qml/Quick imports asserted) + compiles `relink-test/main.cpp` against Qt6Core import lib and runs it with deploy-only PATH → RELINK OK proves the deployed Qt6Core.dll is a usable dynamic dependency (re-linkability). Both checks PASS, exit 0.
- `LGPL-COMPLIANCE.md` documents the evidence, relink procedure, and the 3-year source offer (LGPL §6(d)); `THIRD-PARTY-NOTICES.txt` extended with the Qt LGPLv3 notice + source offer and ships next to wisp.exe (deploy folder + installer).
- Clean-VM runbook (D-16) authored with all 8 steps + failure notes, executed by the user on pristine Win10 22H2 and Win11 24H2 VMs, and **user-approved** — all 8 steps PASS on both VMs (2026-08-11).

## task Commits

Each task was committed atomically:

1. **task 1: Installer pipeline — NSIS install, installer.nsi, build-installer.ps1, build gate** - `ef9be83` (feat)
2. **task 2: LGPL compliance evidence — relink test + verify-lgpl.ps1 + docs** - `993a70c` (feat)
3. **task 3: Clean-VM validation runbook + human verification** - `28a8684` (docs: runbook authored + automated checks verified) + `cf6639b` (docs: user-approved results recorded after checkpoint approval)

**Plan metadata:** (final docs commit is the orchestrator's wave-completion commit — this SUMMARY.md is committed by the executor as required)

## Files Created/Modified
- `packaging/installer.nsi` - Per-user NSIS script: no UAC, `%LOCALAPPDATA%\Programs\wisp`, Start Menu shortcut, HKCU uninstall keys, registry-gated VC_redist exec, locked MUI2 copy
- `packaging/build-installer.ps1` - deploy.ps1 → official VC_redist download (>1MB sanity) → makensis; exits non-zero on failure; echoes installer path/size
- `packaging/verify-lgpl.ps1` - dumpbin /DEPENDENTS gate + relink test compiled with cl and run against deploy-only PATH; PASS/FAIL lines; exit 0 only on both green
- `packaging/relink-test/main.cpp` - minimal QCoreApplication binary printing LibrariesPath + "RELINK OK" (dynamic Qt6Core import-lib link)
- `packaging/LGPL-COMPLIANCE.md` - evidence doc: how to run verification, what the relink test proves, source offer (LGPL §6(d), 3-year validity), notices shipping locations
- `packaging/THIRD-PARTY-NOTICES.txt` - Qt LGPLv3 notice + source-offer paragraph (extended, not replaced)
- `packaging/VM-RUNBOOK.md` - 8-step clean-VM checklist with expected outcomes + failure notes; results table filled with PASS for all steps on both VMs, verification date 2026-08-11, status user-approved

## Decisions Made
- VC_redist resolved via build-time download (official aka.ms URL) carried inside the installer, executed only on registry-check failure — avoids network dependency on target machines and double-installs (RESEARCH Open Question 1 resolution).
- Installer output path relative to the .nsi (`..\build\deploy\wisp-setup.exe`) so builds are deterministic regardless of makensis working directory.
- Relink test PATH isolates the deploy folder (`build\deploy\wisp`) with `C:\Qt` absent — proving the deployed DLLs, not the dev install, satisfy the binary (RESEARCH Pitfall 5).
- No code signing in v1 (accepted risk T-06-09, deferred to v2 per CONTEXT).

## Deviations from Plan

### Checkpoint: Human-verify gate (planned)

**1. task 3 human gate — approved by user**
- **Type:** planned `checkpoint:human-verify` (blocking), documented in plan frontmatter `user_setup` (Clean Windows VMs)
- **Flow:** executor authored VM-RUNBOOK.md (commit `28a8684`), then stopped at the checkpoint and returned. User ran the runbook on pristine Win10 22H2 and Win11 24H2 VMs (no dev tools, no Qt, no VC runtime), confirmed all 8 steps pass on both, and responded **approved**. Continuation executor recorded the results.
- **Outcome:** results table in `packaging/VM-RUNBOOK.md` filled with PASS × 16, verdict APPROVED per VM, verification date 2026-08-11, status user-approved.
- **Committed in:** `cf6639b`

---

**Total deviations:** 1 (planned human-verify checkpoint, no auto-fix deviations — plan executed exactly as written)
**Impact on plan:** None — checkpoint was part of the plan design (D-16 requires manual clean-machine verification that cannot be automated).

## Issues Encountered
- None during execution. All planned automated gates passed first-run: `build-installer.ps1` exit 0 (wisp-setup.exe 73.7 MB), `verify-lgpl.ps1` exit 0 with both checks PASS, runbook content checks green.

## User Setup Required
Clean Windows VMs were the plan's `user_setup` item: Win10 22H2 + Win11 24H2 snapshots (no dev tools, no Qt, no VC runtime), plus the user executing the 8-step runbook. Completed and approved 2026-08-11. No further external service configuration required.

## Next Phase Readiness
- Release gate (SYS-04) is satisfied: installer builds clean, LGPL compliance is mechanically evidenced and documented, and clean-machine behavior is user-approved on both supported Windows versions.
- Ready for: packaging/release wrap-up (v2 items: code signing, MSIX, winget manifest), or the post-release milestone.
- Known follow-ups (deferred, per CONTEXT): code signing for the installer; Win10 support horizon planning (Qt 6.12 will be the last Qt line supporting Windows 10).

---

*Phase: 06-tray-settings-autostart-packaging*
*Completed: 2026-08-11*

## Self-Check: PASSED

Verified 2026-08-11 (continuation executor):
- All 9 claimed files exist (7 packaging/ artifacts + `build/deploy/wisp-setup.exe` 73.7 MB + this SUMMARY.md).
- All 4 commits present in git history: `ef9be83`, `993a70c`, `28a8684`, `cf6639b`.
- Automated acceptance re-run green: 19/19 grep acceptance checks pass; `verify-lgpl.ps1` exits 0 (dumpbin Qt6 imports + RELINK OK against deployed DLLs); task-3 runbook checks pass incl. user-approved results.
