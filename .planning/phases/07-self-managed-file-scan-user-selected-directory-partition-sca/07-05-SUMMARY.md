# 07-05 Summary: Settings "Scan locations" section — Theme growth, store keys, controller surface, QML

**Status:** COMPLETE (3 tasks, ctest 20/20 green)
**Commits:** `2280d57` (T1 source), `1cc6a26` (T1 tests), `269530f` (T2), `8f77559` (T3)

## Task 1 — Theme tokens + SettingsStore scan keys (qml/Theme.qml, src/core/SettingsStore.{h,cpp}, tests/tst_settings.cpp)

- Window geometry grows via tokens (OQ1: growth, not ScrollView): `settingsWindowHeight` 360→560, `settingsSurfaceHeight` 328→528; new tokens `settingsRowScan: 158`, `settingsRowScanItem: 28`, `settingsRowScanRoots: 56`; budget comment updated (488 ≤ 528, 40px slack).
- SettingsStore write side: `setScanRoots` — one normalization site via `QDir::toNativeSeparators` (Pitfall 5), empty entries dropped, duplicates collapsed order-preserved, `sync()` after every write; `setScanIntervalMinutes` — `qBound(1, x, 1440)` (OQ4), clamp re-applied at READ so a tampered INI reads a legal value. No signals on the store (SettingsWindow live-read precedent).
- tst_settings: 4 new suites (round-trip with native separators, empty/dup cleaning, clamped interval incl. missing-key default 10, reopen durability). Two MSVC braced-init/QStringLiteral rules re-learned (hoist expected values).

## Task 2 — SettingsWindow controller surface (src/ui/SettingsWindow.{h,cpp}, src/app/main.cpp, tests/tst_shell.cpp)

- Ctor gains `ScanService *scanService` (before parent); Q_PROPERTYs `scanRoots`/`scanIntervalMinutes`/`lastScanSummary` + Q_INVOKABLEs `addScanRoot`/`removeScanRoot(int)`/`setScanInterval(int)`/`scanNow`; `refreshScanState()` re-emits on `open()` (D-10 state-read); live `lastScanSummary` via `scanStateChanged` connect.
- addScanRoot: picker → dup-guard → store → `requestScan()` (D-09 first-root trigger); removeScanRoot: index-guarded → re-scan (empty → walkAndDelta wipe → NoRoots); setScanInterval → `refreshInterval()`; scanNow → `requestScan()` (single-flight gate inside).
- **DEVIATION (build-forced, documented)**: QFileDialog lives in QtWidgets, which wisp_core does NOT link — the folder picker is injected via a `FolderPicker` std::function seam (FileSearch::setAddExeDialog precedent, main.cpp:124-127), wired in main.cpp to `QFileDialog::getExistingDirectory`. Plan literal (QFileDialog inside SettingsWindow.cpp) would have forced Widgets onto all 17 wisp_core consumers.
- tst_shell updated: ctor call site + height expectation 360→560.

## Task 3 — SettingsWindow.qml Scan locations section

- Section after autostartRow: hairline + "Scan locations"/"Folders wisp scans for files and folders" title block left; right controls column (260 wide, 128 ≤ 158): height-capped QtQuick.Controls ScrollView root list (Token heights, per-row elided path + Remove hover text) OR "No folders yet — add one below" placeholder, ± interval row ("Scan every N min", clamp in store), accent "Scan now" button + elided last-scan summary ("Not scanned yet" fallback). Token-only, null-safe `settingsController ? … : …` guards, no literals beyond the strings; window height binding unchanged (token-driven).

## Verification

- Build clean (qmlcachegen validates); ctest 20/20 green; startup smoke: wisp.exe launches, stays resident, killed cleanly (manual visuals pending phase UAT).

## Notes for later plans

- Manual-only items for phase validation: Settings section renders at 560, add/remove via native picker, interval persists + re-arms, Scan now runs, summary appears, D-04 prompt on empty query with no roots.
- All D-01..D-10 of the phase roadmap are now implemented across 07-01..07-05.
