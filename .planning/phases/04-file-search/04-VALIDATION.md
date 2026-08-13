---
phase: 04
slug: file-search
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-08-10
---

# Phase 04 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Qt Test (Qt6::Test) — 3 new suites (tst_search, tst_filesearch, tst_history) + extensions to tst_model and tst_launch; suites auto-discovered via ctest |
| **Config file** | CMakeLists.txt BUILD_TESTING block (existing pattern, per-test qt_add_executable blocks) |
| **Quick run command** | `ctest --test-dir build/dev -R "tst_search|tst_filesearch|tst_history|tst_model|tst_launch" --output-on-failure` |
| **Full suite command** | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure` |
| **Estimated runtime** | ~60-90 seconds (build+link dominates; test suites run in seconds) |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/dev -R <affected-suite> --output-on-failure`
- **After every plan wave:** Run the full suite command above
- **Before `/gsd-verify-work`:** Full suite must be green (existing 10 suites + new 3)
- **Max feedback latency:** ~90 seconds

---

## Per-task Verification Map

| task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 04-01-01 | 01 | 1 | LAUN-02 | T-04-01 / — | Firewall contracts; no user input ever string-concatenated into SQL (grep gate: no `+=`/`append` on the SQL buffer) | compile + grep gate | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 \| Select-String -Pattern "error\|warning" -NotMatch` | ✅ (created in-task) | ⬜ pending |
| 04-01-02 | 01 | 1 | LAUN-02 | T-04-02 | Status mapping (7 enum values incl. PAUSED-not-troubled), post-filter predicate (.exe ci / folder kept), WHERE-restriction content gate | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_search --output-on-failure` | ✅ (created in-task, RED-first) | ⬜ pending |
| 04-02-01 | 02 | 2 | LAUN-02 | T-04-04 / — | FileSearch contract (firewall-clean: no win/ includes); WinSearchQuery failure out-param (RESEARCH §2 Unavailable path) | compile + grep gate | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 \| Select-String -Pattern "error\|warning" -NotMatch` | ✅ (created in-task) | ⬜ pending |
| 04-02-02 | 02 | 2 | LAUN-02 | T-04-04, T-04-06 | Debounce ~150ms (qWait), generation stale-drop, empty-query bypass (D-14), Disabled/Unavailable skip query / Building queries + status, query-failure → Unavailable, tracked-source merge, quiet fill-in (no extra signals), status copy single-homed in C++ | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_filesearch --output-on-failure` | ✅ (created in-task, RED-first) | ⬜ pending |
| 04-03-01 | 03 | 3 | LAUN-02 | T-04-08 | QSettings INI round-trip (record/reload/count/manual store), trackedExecutables shape (Source::File), native-separator key normalization, union dedupe, UWP skip | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_history --output-on-failure` | ✅ (created in-task, RED-first) | ⬜ pending |
| 04-03-02 | 03 | 3 | LAUN-02, LAUN-03 | T-04-07, T-04-09 | File Enter = non-elevated open; elevated on file/folder = silent normal (D-05, zero signals); revealInExplorer quoted + native path; reveal no-op for apps + D-12 freeze + D-13 dismiss; launch tracking via default reporter; no QProcess / no runas for files (grep gates) | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_launch --output-on-failure` | ✅ (extends existing) | ⬜ pending |
| 04-04-01 | 04 | 2 | LAUN-02 | T-04-10 / — | Merge contract: Row struct, setFileResults, IsFolderRole, kMaxFileRows/kPathMatchScore constants; RED suites (merge/cap/tier/stale/subtitle/glyph/D-14/selection/ranges) | unit (TDD, RED) | `grep gates on ResultsModel.h + tst_model.cpp slot presence` | ✅ (extends existing) | ⬜ pending |
| 04-04-02 | 04 | 2 | LAUN-02 | T-04-11, T-04-12 | Merge implementation: interleave score order (D-01), cap 5 file rows (D-03, apps never dropped), path-only base tier (D-07), subtitle = full path (D-02), model-side generation guard (D-15), selection clamp; all 9 existing suites green | unit (TDD, GREEN) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_model --output-on-failure` | ✅ (extends existing) | ⬜ pending |
| 04-05-01 | 05 | 4 | LAUN-02 | T-04-13 | Wiring order (FileSearch/LaunchHistory constructed before load, seams + context property + resultsReady→setFileResults; worker starts on query, off hotkey path); explicit status ordinal map; single queryFiles call site | compile + grep gate | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure` | ✅ (extends existing) | ⬜ pending |
| 04-05-02 | 05 | 4 | LAUN-02, LAUN-03 | T-04-14, T-04-15 | Status row non-selectable overlay rendering the C++-owned copy (verbatim, literal gate), Ctrl+Enter → reveal (branch order preserved), folder glyph, add-exe pinned row (native dialog), Theme-token-only | compile + literal gate + grep gate | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 \| Select-String -Pattern "qmllint\|error\|warning" -NotMatch; ctest --test-dir build/dev -R tst_shell --output-on-failure` | ✅ (extends existing) | ⬜ pending |
| 04-05-03 | 05 | 4 | LAUN-02, LAUN-03 | — | Human-verified: type .exe name → file row; Enter opens; Ctrl+Enter reveals; Ctrl+Shift+Enter silent-normal; folder glyph + open; status row on `Stop-Service WSearch`; add-exe flow persists; typing stays smooth | checkpoint:human-verify (blocking) | `ctest --test-dir build/dev --output-on-failure` (pre-gate green) + 8-step manual checklist | ✅ (manual) | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `build.ps1` + CMake BUILD_TESTING harness — existing from Phase 02/03 (10 green suites)
- [x] New test suites created IN-TASK with RED-first ordering (tst_search, tst_filesearch, tst_history) — no pre-phase stubs needed
- [x] No new dependencies: raw OLE DB COM uses SDK headers already on the toolchain (atldbcli.h verified ABSENT — ATL path rejected in RESEARCH §1)

*Existing infrastructure covers all phase requirements; Nyquist gate satisfied by in-task RED-first suite creation.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live Windows Search query returns .exe rows | LAUN-02 | OLE DB needs a real SystemIndex + live desktop session | Open launcher, type an .exe name (e.g. "notepad") → file row appears with path subtitle; Enter launches it with its default app |
| Ctrl+Enter reveals containing folder | LAUN-03 | Explorer shell interaction only exists live | Ctrl+Enter on a file result → Explorer opens with the file selected |
| Folder rows: glyph + Enter opens folder | LAUN-02 | Real folders in the index + Explorer open | Type a folder name → row shows glyph; Enter opens the folder in Explorer |
| Indexer Disabled status row | LAUN-02 | Requires toggling the Windows Search service | `Stop-Service WSearch` → type a query → "Indexing is turned off — enable Windows Search to find files" row appears, apps still work; `Start-Service WSearch` after |
| "Add executable…" flow | LAUN-02 | Native file dialog interaction | Click the pinned row → pick an .exe → it appears in results (even on an unindexed path); persists across restart |
| Typing feel with file queries active | LAUN-02 | Perceived latency can't be asserted headlessly | Type continuously — apps appear instantly, file rows fill in ~150ms after pause, no stutter/freeze |
| Launch tracking persistence | LAUN-02 | Real INI file + restart cycle | Launch an app, quit wisp, relaunch, type its name → still findable via tracked catalog |

*Live-search/live-launch behaviors are deliberately out of the automated suite; everything else has a machine-verifiable command.*

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or in-task test creation (Nyquist: no MISSING wildcards)
- [x] Sampling continuity: every task carries `build.ps1`/`ctest` commands — no 3 consecutive tasks without automated verify
- [x] Wave 0 coverage: existing harness suffices; suites created/extended RED-first inside tasks 04-01-02, 04-02-02, 04-03-01, 04-04-01/02 (tst_model/launch extensions follow the same RED-first discipline)
- [x] No watch-mode flags
- [x] Feedback latency < 90s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending — set after full-suite green + checkpoint human-verification
