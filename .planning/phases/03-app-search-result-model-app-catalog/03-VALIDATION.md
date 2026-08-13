---
phase: 03
slug: app-search-result-model-app-catalog
status: complete
nyquist_compliant: true
wave_0_complete: true
created: 2026-08-09
---

# Phase 03 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Qt Test (Qt6::Test) — 5 new suites: tst_matcher, tst_model, tst_enum, tst_catalog, tst_launch; suites auto-discovered via ctest |
| **Config file** | CMakeLists.txt BUILD_TESTING block (existing 02 pattern, per-test qt_add_executable blocks) |
| **Quick run command** | `ctest --test-dir build/dev -R "tst_matcher|tst_model|tst_enum|tst_catalog|tst_launch" --output-on-failure` |
| **Full suite command** | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev --output-on-failure` |
| **Estimated runtime** | ~60-90 seconds (build+link dominates; test suites run in seconds) |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/dev -R <affected-suite> --output-on-failure`
- **After every plan wave:** Run the full suite command above
- **Before `/gsd-verify-work`:** Full suite must be green (existing 5 suites + new 5)
- **Max feedback latency:** ~90 seconds

---

## Per-task Verification Map

| task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 03-01-01 | 01 | 1 | LAUN-05 | — | N/A (type contract only) | compile | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 | Select-String -Pattern "error|warning" -NotMatch` | ✅ (created in-task) | ✅ green |
| 03-01-02 | 01 | 1 | LAUN-01, LAUN-05 | T-03-01-01 / — | Pure function: no I/O, no state; deterministic score/ranges; perf smoke <5ms (D-06 UI-thread budget) | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_matcher --output-on-failure` | ✅ (created in-task, RED-first) | ✅ green |
| 03-01-03 | 01 | 1 | LAUN-01, LAUN-05 | T-03-01-02 / — | Snapshot freeze at keypress (D-12); selection clamped; no crash on empty/1-row | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_model --output-on-failure` | ✅ (created in-task, RED-first) | ✅ green |
| 03-02-01 | 02 | 2 | LAUN-01 | T-03-02-01 | Per-link exception/HRESULT guard + continue; never aborts batch; both Programs roots mandatory (RESEARCH §1) | compile + unit (fixture in 03-02-03) | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 | Select-String -Pattern "error|warning" -NotMatch` | ✅ (created in-task) | ✅ green |
| 03-02-02 | 02 | 2 | LAUN-01 | T-03-02-02 / — | No WindowsApps/registry access; AUMID built not guessed (RESEARCH §2); WinRT init contract documented | compile + grep gate | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 | Select-String -Pattern "error|warning" -NotMatch` | ✅ (created in-task) | ✅ green |
| 03-02-03 | 02 | 2 | LAUN-01 | T-03-02-01 | Garbage .lnk skipped without abort (fixture-driven scanRoots); junk filter keeps only launchable, named apps; AUMID exact format | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_enum --output-on-failure` | ✅ (created in-task, RED-first) | ✅ green |
| 03-03-01 | 03 | 3 | LAUN-01 | T-03-03-01 / — | Worker-thread build off hotkey path (D-08); single-flight; silent atomic swap; deterministic dedupe (.lnk wins, D-10); age refresh 10-min | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_catalog --output-on-failure` | ✅ (created in-task, RED-first) | ✅ green |
| 03-04-01 | 04 | 4 | LAUN-01, LAUN-04 | T-03-04-01 / T-03-04-02 | UAC cancel = quiet no-op (D-11); elevated only for desktop apps (Ctrl+Shift+Enter, LAUN-04); no QProcess (grep gate); SEE_MASK_NOCLOSEPROCESS | compile + grep gate | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 | Select-String -Pattern "error|warning" -NotMatch` | ✅ (created in-task) | ✅ green |
| 03-04-02 | 04 | 4 | LAUN-01, LAUN-04 | T-03-04-03 | Snapshot freeze (D-12); instant dismiss (D-13); admin refusal surfaced as quiet hint signal | unit (TDD) | `powershell -ExecutionPolicy Bypass -File build.ps1; ctest --test-dir build/dev -R tst_launch --output-on-failure` | ✅ (created in-task, RED-first) | ✅ green |
| 03-05-01 | 05 | 5 | LAUN-01, LAUN-04, LAUN-05 | — | Wiring order: tray → hotkey → context props before loadFromModule; catalog start once, off hotkey path; ensureFresh only in visibleChanged | compile + grep gate | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 | Select-String -Pattern "error|warning" -NotMatch; ctest --test-dir build/dev --output-on-failure` | ✅ (extends existing) | ✅ green |
| 03-05-02 | 05 | 5 | LAUN-01, LAUN-05 | — | Verbatim copy strings (RESEARCH §7); Theme-token-only visuals (literal gate); currentIndex: selectedIndex binding; Keys.forwardTo | compile + literal gate + unit | `powershell -ExecutionPolicy Bypass -File build.ps1 2>&1 | Select-String -Pattern "qmllint|error|warning" -NotMatch; Select-String literal gate` | ✅ (extends existing) | ✅ green |
| 03-05-03 | 05 | 5 | LAUN-01, LAUN-04, LAUN-05 | — | Human-verified slice: search/nav/launch/elevation/dismiss/feel | checkpoint:human-verify (blocking) | `ctest --test-dir build/dev --output-on-failure` (pre-gate green) + 10-step manual checklist | ✅ (manual) | ✅ green |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `build.ps1` + CMake BUILD_TESTING harness — existing from Phase 02 (5 green suites: tst_shell, tst_hotkey, tst_launcher, tst_capture, tst_tray)
- [x] New test suites are created IN-TASK with RED-first ordering (tst_matcher, tst_model, tst_enum, tst_catalog, tst_launch) — no pre-phase stubs needed
- [x] C++/WinRT include wiring (`$ENV{WindowsSdkDir}Include/*/cppwinrt` glob) lives inside 03-02-02 — no separate Wave 0

*Existing infrastructure covers all phase requirements; Nyquist gate satisfied by in-task RED-first suite creation.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live Start Menu scan hits the golden list (Calculator/Terminal/Notepad) | LAUN-01 | Real FOLDERID_Programs/CommonPrograms content + real COM parse only exist on the live desktop | Open launcher, type "cal" → Calculator ranks first; "term" → Terminal; "note" → Notepad |
| UWP/Store app enumeration | LAUN-01 | PackageManager requires real installed packages | Type a Store app name (e.g. Calculator UWP build, Photos) → row appears; Enter launches it |
| Real launch lifetime (normal + elevated) | LAUN-04 | UAC prompt + process semantics only exist live | Enter launches normally; Ctrl+Shift+Enter on Notepad → UAC → elevated Notepad; confirm app stays open after launcher closes |
| UAC cancel is quiet | LAUN-04 | User-interactive prompt | Ctrl+Shift+Enter then Cancel UAC → no error box, no crash, launcher dismisses |
| UWP elevation refusal hint | LAUN-04 | Requires a Store app name + Ctrl+Shift+Enter combo | Ctrl+Shift+Enter on a Store app → transient hint "Only desktop apps can run as administrator", no UAC, no crash |
| Hotkey→open latency + animation integrity | LAUN-01 | Perceived feel / 60fps can't be asserted headlessly | Press Alt+Space → popup appears immediately with the 02 animation; open/close/launch never janky |
| Hover-select + keyboard nav coexistence | LAUN-05 | Real input focus semantics | Type "calc", press ↑/↓ → selection moves (caret NOT moving); hover a row → surfaceSecondary highlight; click → launches (checkpoint task 3 steps) |
| Broken-link resilience on real Start Menu | LAUN-01 | Real-world .lnk variety (uninstall leftovers, dead targets) | Full scan completes without crash; app list does not contain dead shortcuts; launcher remains responsive |

*Live-scan/live-launch behaviors are deliberately out of the automated suite; everything else has a machine-verifiable command.*

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or in-task test creation (Nyquist: no MISSING wildcards)
- [x] Sampling continuity: every task carries `build.ps1`/`ctest` commands — no 3 consecutive tasks without automated verify
- [x] Wave 0 coverage: existing harness suffices; suites created RED-first inside tasks 03-01-02/03, 03-02-03, 03-03-01, 03-04-02
- [x] No watch-mode flags
- [x] Feedback latency < 90s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved 2026-08-10 (full suite 10/10 green; all automated gates pass; checkpoint human-verified)