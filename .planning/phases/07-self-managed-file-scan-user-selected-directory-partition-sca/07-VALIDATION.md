---
phase: 07
slug: self-managed-file-scan-user-selected-directory-partition-sca
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-08-14
---

# Phase 07 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Qt Test (CTest/QTest) — existing `tests/tst_*` pattern |
| **Config file** | CMakeLists.txt (root) — `enable_testing()` + per-tst targets |
| **Quick run command** | `ctest --test-dir build --output-on-failure` |
| **Full suite command** | same as quick run (project convention — single suite) |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build --output-on-failure`
- **After every plan wave:** Run the full suite + build the app
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** ~15 seconds

---

## Per-task Verification Map

| task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 07-01-0x | 01 | 1 | LAUN-02/03 | T-07-xx / — | N/A (no external input; local scan only) | unit | ctest | ❌ W0 | ⬜ pending |
| 07-02-0x | 02 | 1-2 | LAUN-02/03 | T-07-xx / — | N/A | unit | ctest | ❌ W0 | ⬜ pending |
| 07-03-0x | 03 | 2 | LAUN-02/03 | T-07-xx / — | N/A | unit | ctest | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*
*Full map filled in during execution — one row per task with `<automated>` verify.*

---

## Wave 0 Requirements

- [ ] `tests/tst_scan.cpp` — stubs for: delta-walk logic (mtime memo), skip-list filter, FileIndex serialize/deserialize round-trip, dedupe (catalog-vs-scan)
- [ ] `tests/tst_fuzzy.*` reuse — existing ranker tests stay (03/04)
- [ ] Remove `tests/tst_search.cpp` + WinSearchQuery references (Phase-4 Windows Search tests die with the backend)
- [ ] CMakeLists.txt — replace `tst_search` target with `tst_scan`; drop WinSearchQuery from wisp_win list

*If none: "Existing infrastructure covers all phase requirements."*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Scan of a real user partition (e.g. C:\) stays smooth while typing | LAUN-02/03 | Perf/feel — requires real filesystem + interactive typing | Add C:\ as scan root, type in launcher during a full re-scan; typing must never stutter |
| Index survives relaunch with no re-walk | LAUN-02/03 | Requires app lifecycle | Scan, quit, relaunch — results appear instantly; check index file mtime unchanged |
| Scanner degrades when a root is removed (USB drive) | LAUN-02/03 | Requires removable hardware | Scan a USB path, unplug, relaunch, type — no crash, row hints at missing root |
| Settings "Scan locations" section UX | LAUN-02/03 | Visual/QML feel | Open Settings → scan section: add/remove roots, interval, Scan now, last-scan summary |
| 10-min interval actually re-scans changed dirs | LAUN-02/03 | Time-based | Drop a new .exe in a root, wait past interval (or lower interval to 1 min), verify it appears |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (tst_scan stubs, tst_search removal)
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending