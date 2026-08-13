---
phase: 05-theme-visual-polish
plan: 2
subsystem: windows-integration
tags: [qt6, cpp, lru, qmutex, qhash, qlist, qttest, icontheory, bounded-cache]

# Dependency graph
requires:
  - phase: 04-file-search
    provides: LaunchHistory QMutex WR-01 discipline (one lock per accessor, non-recursive) — the copied threading pattern for the LRU
  - phase: 05-01
    provides: icon seams (WinIconExtractor/WinUwpLogo) whose results this cache stores; CMakeLists ownership serialization
provides:
  - "IconCache: bounded in-memory LRU (QHash + QList order, QMutex-guarded, default cap 500 ≈ 8 MB) with the exact API plan 05-04 consumes: get/insert/size, null-on-miss, null-insert ignored, ctor IconCache(int capacity = 500)"
  - "tst_iconcache suite (6 cases): capHonored, oldestEvicted, hitReorders, missReturnsNull, insertNullIgnored, threadSafeInsertGet (QtConcurrent race) — ctest 15/15 overall"
affects: [theme-visual-polish (plan 05-04: IconProvider mounts this cache), packaging (memory footprint bound)]

# Tech tracking
tech-stack:
  added: [none — Qt Core only (QHash/QList/QMutex/QImage implicit sharing), in-tree]
  patterns: ["Bounded LRU: QHash map + QList order list (front = LRU, back = MRU); get/insert reorder via removeOne+append (O(n) at n ≤ 500); eviction loop removeFirst while over cap — the boundedness proof", "One non-recursive QMutexLocker per public method — no nested public calls (WR-01/PATTERNS reentrancy contract)", "Failures never cached: miss returns null QImage, null images silently ignored on insert (D-03/D-16 discipline)", "Implicit-sharing return: get() returns the stored QImage by value — cheap copy, no detach"]

key-files:
  created: [src/core/IconCache.h, src/core/IconCache.cpp, tests/tst_iconcache.cpp]
  modified: [CMakeLists.txt]

key-decisions:
  - "LRU order kept in a single QList<QString> (removeOne+append on touch) instead of a doubly-linked list — O(n) at n ≤ 500 is fine; one canonical order list keeps map/order provably consistent"
  - "size() uses m_order.size() (not m_map.size()) — m_order is the eviction authority; the two are kept in lockstep by construction"
  - "Capacity 0 is a valid degenerate config (cache everything off); negative capacity is not guarded — internal utility, callers use 500/2 only (no over-engineering)"
  - "REQUIREMENTS.md VISU-02 stays Pending until phase completion (LAUN-02 precedent); SUMMARY frontmatter lists it per template"

patterns-established:
  - "Bounded LRU utility shape: plain non-QObject class, mutable QMutex member, READ-MODIFY-WRITE under one lock per public method (LaunchHistory.h:16-53 shape)"
  - "Race smoke test shape: atomic start flag + QtConcurrent::run writer/reader lambdas + waitForFinished (tst_history.cpp:173-210 verbatim)"

requirements-completed: [VISU-02]

# Metrics
duration: 30min
completed: 2026-08-10
---

# Phase 05 Plan 02: Bounded LRU Icon Cache Summary

**Bounded in-memory LRU icon cache (D-03): IconCache stores extracted 64px QImages keyed by opaque provider ids under a QMutex, cap 500 ≈ 8 MB enforced by an insert-time eviction loop, misses return null and failures are never cached — proven by a 6-case QtTest suite including a QtConcurrent writer/reader race (ctest 15/15).**

## Performance

- **Duration:** ~30 min
- **Started:** 2026-08-10T19:18:59Z (first task commit)
- **Completed:** 2026-08-10T19:23:35Z (last task commit)
- **Tasks:** 2
- **Files modified:** 4 (3 created, 1 modified)

## Accomplishments

- **IconCache (src/core)**: plain non-QObject class in the LaunchHistory.h:16-53 shape — `mutable QMutex m_mutex`, `QHash<QString,QImage> m_map`, `QList<QString> m_order` (front = LRU, back = MRU), `int m_capacity = 500`. API exactly as 05-04 consumes: `QImage get(key)` (hit → reorder to MRU + implicit-shared copy; miss → null QImage, never cached), `void insert(key, img)` (null image silently ignored — failure-not-cached contract; existing key updated + reordered; eviction loop `removeFirst` while over cap — the boundedness proof), `int size()` (m_order.size(), the eviction authority).
- **Threading contract documented in the header**: single provider thread per engine (plan 05-04) + QMutex for reentrancy/future concurrent use; every public method takes and releases the lock exactly once (non-recursive — no nested public calls, WR-01 discipline copied from LaunchHistory).
- **tst_iconcache**: 6 green cases (8 PASS incl. init/cleanup) — capHonored (501 inserts into cap 500 → 500), oldestEvicted (insertion-order eviction), hitReorders (get() promotes to MRU so the other key gets evicted next), missReturnsNull, insertNullIgnored, threadSafeInsertGet (atomic-start QtConcurrent writer inserting 500 distinct keys + reader doing 250 random gets → size 500, no torn state). 8×8 ARGB32 fixtures per key as planned.
- **CMake wiring**: `src/core/IconCache.cpp` added to the wisp_core source list (LNK2019 rule); `tst_iconcache` target + `add_test` + `ENVIRONMENT_MODIFICATION` entry. Full build 33/33 targets link clean; full regression ctest **15/15 passing** (14 existing + tst_iconcache 0.09s).

## Task Status

| # | Task | Commits | Status |
|---|------|---------|--------|
| 1 | IconCache.h + IconCache.cpp — bounded LRU with QMutex discipline | `31f7d62` | Done |
| 2 | tst_iconcache.cpp + CMakeLists.txt wiring | `4b023f0` | Done |

## Verification

- Plan greppable gates ✓: `class IconCache`, `mutable QMutex m_mutex`, `QImage get(`, `void insert(`, `m_capacity`, `= 500` in IconCache.h; `QMutexLocker` ×3 (one per public method), `removeFirst` in IconCache.cpp; `QTEST_MAIN(TstIconCache)`, `capHonored`, `oldestEvicted`, `hitReorders`, `threadSafeInsertGet`, `QtConcurrent` in tst_iconcache.cpp; `IconCache.cpp` in CMakeLists.txt wisp_core list.
- `cmake --build build/dev` — full build clean; `ctest --test-dir build/dev` — **15/15 passing**.
- `tst_iconcache.exe -o file,txt`: `Totals: 8 passed, 0 failed` (init/cleanup + 6 cases).

## Files Created/Modified

- `src/core/IconCache.h` — bounded LRU contract + threading doc comment; plain class, cap configurable (default 500)
- `src/core/IconCache.cpp` — get/insert/size with one QMutexLocker each; removeOne+append reorder; removeFirst eviction loop
- `tests/tst_iconcache.cpp` — 6-case suite incl. QtConcurrent race smoke (tst_history shape)
- `CMakeLists.txt` — wisp_core source + tst_iconcache target + add_test + ENVIRONMENT_MODIFICATION

## Decisions Made

- Single QList order list with removeOne+append on touch (O(n) at n ≤ 500) over a linked-list LRU — one canonical structure keeps map/order provably in lockstep; eviction uses `removeFirst()` exactly as the plan gates demand.
- `size()` reads m_order.size() — the order list is the eviction authority; m_order and m_map are kept consistent by construction (insert/evict touch both under the same lock).
- Null-image inserts are dropped before the lock is taken (cheap early return) — the failure-not-cached contract is asserted by insertNullIgnored.
- No defensive negative-capacity clamp: the cache is an internal utility with fixed call sites (default 500 in 05-04, 2/500 in tests) — added complexity for no reachable failure (ponytail principle).
- VISU-02 stays Pending in REQUIREMENTS.md until phase close (LAUN-02 precedent) — listed in frontmatter per template only.

## Deviations from Plan

### Auto-fixed Issues

**1. [Gate/counting — 05-01 precedent] `rg -c "tst_iconcache"` counts 5, plan expects 3**
- **Found during:** task 2 (CMakeLists wiring)
- **Issue:** The plan gate expects exactly 3 `tst_iconcache` occurrences, but valid CMake normal form produces 5: `qt_add_executable(tst_iconcache …)`, `target_link_libraries(tst_iconcache …)`, `add_test(NAME tst_iconcache COMMAND tst_iconcache)` (2), and the `set_tests_properties` ENVIRONMENT_MODIFICATION entry. Same discrepancy 05-01 hit with `tst_icons` (4 vs 3).
- **Fix:** Kept idiomatic CMake (5 occurrences); gate deviation documented rather than contorting the build file — per the 05-01 precedent and the wave-brief instruction.
- **Files modified:** CMakeLists.txt
- **Verification:** ctest runs tst_iconcache (registered, green, in the env-modified list)
- **Committed in:** 4b023f0 (task 2 commit)

---

**Total deviations:** 1 (gate/counting — no code deviations)
**Impact on plan:** None — the deviation is purely the documented count discrepancy between the plan's grep expectation and idiomatic CMake. All behavior gates (target builds, test runs, 100% pass) are met.

## Issues Encountered

1. **`rg` not on PATH in this shell** — the plan's acceptance greps were executed with the equivalent project grep tool instead (same patterns, verified matches). Environment, not code.
2. **Standalone compile check needed /Zc:__cplusplus** — a direct `cl` syntax check of IconCache.cpp needed the `/Zc:__cplusplus` flag (Qt requirement) since the file wasn't yet in any CMake target at task-1 verify time; compiled clean, then proven again by the real wisp_core build in task 2.
3. **`ninja: no work to do` on the task-1 verify** — expected: IconCache.cpp is only compiled once wired into CMakeLists (task 2); the standalone compile check covered task 1.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **Contract for 05-04** (IconProvider mount): `IconCache::get(const QString &key) -> QImage` (null on miss), `IconCache::insert(const QString &key, const QImage &img)` (no-op for null images), `IconCache::size() -> int`, ctor `IconCache(int capacity = 500)`. The provider's get-miss → extract → insert flow is the exact key_links pattern from this plan's frontmatter.
- `IconCache` is thread-safe for Qt's provider thread (QMutex race-tested); 05-04 must keep the provider's image URLs percent-encoded (05-01 contract) and remember `Image { cache: false }` in QML (05-05) — the LRU is the only cache (D-03, Pitfall 2).
- Threat model T-05-06 (DoS self) mitigated: cap enforced in insert() + asserted by capHonored; T-05-08 (concurrency) mitigated by one-lock-per-method + threadSafeInsertGet. T-05-07/T-05-09 accepted by design (keys opaque, in-memory only).
- No blockers or concerns outstanding.

## Self-Check: PASSED

- FOUND: `src/core/IconCache.h`, `src/core/IconCache.cpp`, `tests/tst_iconcache.cpp` (created)
- Commits verified in `git log`: `31f7d62` (feat), `4b023f0` (test)

---
*Phase: 05-theme-visual-polish*
*Completed: 2026-08-10*


