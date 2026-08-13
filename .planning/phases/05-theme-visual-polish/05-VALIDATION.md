---
phase: 5
slug: theme-visual-polish
status: approved
nyquist_compliant: true
wave_0_complete: false
created: 2026-08-10
---

# Phase 5 - Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Qt Test (C++/Qt 6.11), ctest runner |
| **Config file** | none - CMakeLists `set_tests_properties` (13 existing tst_* targets) |
| **Quick run command** | `ctest --test-dir build/dev --output-on-failure -R "tst_icon|tst_settings"` |
| **Full suite command** | `ctest --test-dir build/dev --output-on-failure` |
| **Estimated runtime** | ~60 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/dev -R "tst_iconcache|tst_settings|tst_icons|tst_model|tst_matcher" --output-on-failure`
- **After every plan wave:** Run full `ctest --test-dir build/dev --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** ~60 seconds

---

## Per-task Verification Map

| task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 05-01-t1 | 05-01 | 1 | VISU-02 | T-05-05 | key parse rejects malformed iconRef | unit + spike | `ctest --test-dir build/dev -R tst_icons` | | pending |
| 05-01-t2 | 05-01 | 1 | VISU-02 | T-05-01 | COM per-call MTA, silent fail → null | unit | `ctest --test-dir build/dev -R tst_icons` | | pending |
| 05-01-t3 | 05-01 | 1 | VISU-02 | T-05-03 | manifest first, indirect string, scale probe | unit | `ctest --test-dir build/dev -R tst_icons` | | pending |
| 05-02-t1 | 05-02 | 2 | VISU-02 | T-05-06 | bounded LRU, no unbounded growth | unit | `cmake --build build/dev --target tst_iconcache && ctest --test-dir build/dev -R tst_iconcache` | | pending |
| 05-02-t2 | 05-02 | 2 | VISU-02 | T-05-06 | cap honored, eviction, reorder, race | unit | `ctest --test-dir build/dev -R tst_iconcache` | | pending |
| 05-03-t1 | 05-03 | 3 | VISU-02 | T-05-10 | INI read w/ silent fallback, no %APPDATA% hardcode | unit | `cmake --build build/dev --target tst_settings && ctest --test-dir build/dev -R tst_settings` | | pending |
| 05-03-t2 | 05-03 | 3 | VISU-02 | T-05-10/12 | signal path, corrupt value fallback | unit | `ctest --test-dir build/dev -R tst_settings` | | pending |
| 05-04-t1 | 05-04 | 4 | VISU-02 | T-05-14 | iconKey role from enumerator, malformed refs skipped | unit | `ctest --test-dir build/dev -R tst_model` | | pending |
| 05-04-t2 | 05-04 | 4 | VISU-02 | T-05-15 | provider never caches failures | build + code review (no test target) | `cmake --build build/dev --target wisp_core` | | pending |
| 05-04-t3 | 05-04 | 4 | VISU-02 | T-05-16 | provider registered pre-load, regression | smoke | `ctest --test-dir build/dev` + wisp.exe launch | | pending |
| 05-05-t1 | 05-05 | 5 | VISU-02 | T-05-21 | token-only, silent default fallback | unit + QML smoke | `ctest --test-dir build/dev -R tst_settings` | | pending |
| 05-05-t2 | 05-05 | 5 | LAUN-06 | T-05-18/19 | rich text escaped, no HTML injection | QML smoke | `ctest --test-dir build/dev` | | pending |
| 05-05-t3 | 05-05 | 5 | VISU-02, LAUN-06 | T-05-20 | empty-state gating, scrollbar tokens | manual + visual | live visual checkpoints 1-8 | | pending |

---

## Validation Architecture

See `.planning/phases/05-theme-visual-polish/05-RESEARCH.md` §"Validation Architecture" for the full validation map: Wave-0 test targets (`tst_icons`, `tst_iconcache`, `tst_settings`, `tst_model` extension), the 1-minute id-encoding spike, and per-pattern validation approach (unit tests behind `src/win` seams, QML smoke checks, live-machine visual checks for DPI/derived-shade factors).
