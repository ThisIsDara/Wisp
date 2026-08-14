# 07-02 Summary: ResultsModel D-03 path-set dedupe

**Status:** COMPLETE (2 tasks, ctest 21/21 green)
**Commits:** `4d04a05` (Task 1), `1e1bdf9` (Task 2)

## Task 1 — mergeFiles dedupe (src/core/ResultsModel.cpp)

- `appPaths` set built BEFORE the candidate loop from path-bearing app-channel rows (`m_order`, skipping `fromFiles`); keys are `targetPath.toCaseFolded()` — one consistent fold at one site (T-07-03).
- UWP rows (empty `targetPath`) excluded by the `!p.isEmpty()` guard — they can never suppress a scan row (Pitfall 10).
- Tracked/added rows flow through the app channel → covered by the same set (D-03 scope).
- Suppression in the final emit loop after the `kMaxFileRows` cap check: `appPaths.contains(entryAt(c.row).targetPath.toCaseFolded())`. O(1) per file row — no quadratic pass, cap logic untouched.
- Added `#include <QSet>`.

## Task 2 — tst_model D-03 cases (tests/tst_model.cpp)

- `scanRowSuppressedWhenCatalogHasSamePath_D03` — 1 row, catalog display name wins.
- `suppressionIsCaseFolded_D03` — lower-case file path collides with catalog key.
- `uwpRowNeverSuppressesScanRow_D03` — 2 rows render (Pitfall 10).
- `distinctPathsBothRender_D03` — same display name, different path → 2 rows; subtitle disambiguates source.
- `trackedStyleRowSuppressesScanRow_D03` — Source::File entry on the app channel suppresses the scan duplicate.
- Reused existing helpers (lnkEntry/uwpEntry/fileEntry/displayNameAt); 37 slots total, all pre-existing slots untouched.

## Verification

- Build clean; `ctest --test-dir build/dev -R tst_model` green; full `ctest` 21/21 green.

## Notes for later plans

- Dedupe is now proven before 07-03/07-04 wire scanned entries into the pipeline — no role/score/cap changes were needed (PATTERNS prediction held).
