---
phase: 04-file-search
plan: 05
subsystem: ui-wiring
tags: [qml, cpp, windows-search, debounce, checkpoint, selection-indicator]

# Dependency graph
requires:
  - phase: 04-file-search
    plan: 01
    provides: WinSearchQuery firewall (queryFiles/checkIndexStatus), FileResult, IndexerState
  - phase: 04-file-search
    plan: 02
    provides: FileSearch coordinator seams (QueryFn/StatusFn/TrackedSource/AddExeDialog/AddEntryStore), locked status copy
  - phase: 04-file-search
    plan: 03
    provides: LaunchHistory INI store, LaunchController::setHistory/revealSelected, D-05 silent-normal policy
  - phase: 04-file-search
    plan: 04
    provides: ResultsModel::setFileResults generation merge, IsFolderRole, full-path subtitles
provides:
  - Vertical slice wired end-to-end in main.cpp: FileSearch + LaunchHistory construction, all five seams, dual-pipeline onTextChanged (D-12), resultsReady→setFileResults (D-15)
  - QML UX: indexer status row overlay gated on indexerOk (D-17/D-18), empty-state gate, Ctrl-only reveal branch (LAUN-03), folder ▸ glyph (D-04), pinned Add executable… row (D-11)
  - Human-approved live desktop verification (8/8 steps) of the complete file-search phase
  - Checkpoint-note selection indicator: left-edge accentLight bar marks the current row
affects: [05-theme-visual-polish (icons swap the glyph + initial; selection indicator pre-validates the accent look), 06-tray-settings (settings store)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Dual-pipeline typing: ONE onTextChanged routes the same text to resultsModel.setQuery (instant) and fileSearch.setQuery (debounced) — D-12 feel contract in QML"
    - "Overlay-vs-model discipline: status row is a non-selectable Item overlay (never a model row, never focusable) — D-18"
    - "Explicit ordinal mapping in main.cpp between WinSearchQuery::IndexerState and FileSearch::FileSearchState — never a blind cast (D-16)"

key-files:
  created: []
  modified:
    - src/app/main.cpp
    - qml/MainWindow.qml
    - qml/ResultsRow.qml

key-decisions:
  - "Selection indicator (checkpoint note 2026-08-10): left-edge accentLight bar, width Theme.spaceSm (8px — 4px read thin against the accent selection background), visible only on ListView.isCurrentItem; hover rows never show it"
  - "Ctrl+Enter branch order preserved elevated > reveal > normal in launchFromKey (T-04-15) — the Ctrl-only branch provably fires only without Shift"
  - "Status row + emptyState both keyed off fileSearch.indexerOk so the overlay owns the space only when it must (D-18)"
  - "Pinned Add executable… row takes no focus and never joins the model — click-only, keyboard owns the list (D-11)"

patterns-established:
  - "Injection wiring host: every 04-02 seam is a std::function assigned in main.cpp with the D-XX rationale in a block comment — the app is the only production config point; tests wire fakes"

requirements-completed: [LAUN-02, LAUN-03]

# Metrics
duration: ~25min (execution) + resume session
completed: 2026-08-10
---

# Phase 4 Plan 5: Vertical Slice Wiring + QML UX Summary

**The file-search vertical slice is fully wired and human-verified: main.cpp connects every FileSearch/LaunchHistory seam, the QML renders the indexer status row, Ctrl+Enter Explorer reveal, folder ▸ glyph and pinned Add executable… row — the checkpoint-approved live session passed all 8 steps (13/13 test suites green), and the approval note added a vibrant left selection-bar indicator.**

## Performance

- **Duration:** ~25 min execution (04:53:50Z → checkpoint pause at 05:06) + continuation session for note fix + docs
- **Started:** 2026-08-10T04:53:50Z
- **Completed:** 2026-08-10T05:06:20Z (execution); docs 2026-08-10
- **Tasks:** 2 auto + 1 blocking human checkpoint (approved with note)
- **Files modified:** 3 (main.cpp, MainWindow.qml, ResultsRow.qml)

## Accomplishments

- **main.cpp wiring (task 1):** `LaunchHistory history` + `FileSearch fileSearch` constructed before loadFromModule; context property `fileSearch` exposed; all five seams wired — QueryFn (WinSearchQuery::queryFiles firewall with failure out-param → Unavailable), StatusFn (explicit ordinal switch, never a blind cast), TrackedSource (LaunchHistory::trackedExecutables), AddExeDialog (native QFileDialog, `.exe` filter), AddEntryStore (LaunchHistory::addExecutable); `launch.setHistory` records every wisp launch (D-10); resultsReady→setFileResults connect carries the generation (D-15). Grep gates: `fileSearch` ×20 in main.cpp, `WinSearchQuery::queryFiles` exactly 1 call site.
- **QML UX (task 2):** one `onTextChanged` drives both pipelines (D-12 apps instant / files debounced); `launchFromKey` gains the Ctrl-only `revealSelected()` branch before the else (LAUN-03, T-04-15 branch order); indexer status row overlay gated on `!fileSearch.indexerOk` rendering the verbatim locked copy (D-17/D-18); emptyState gated on `fileSearch.indexerOk`; pinned "Add executable…" row with hairline + hover tint + click → `fileSearch.addExecutable()` (D-11); folder monogram shows the U+25B8 ▸ glyph in accentLight (D-04). tst_shell green — no literal violations (1px hairline + U+25B8 are the declared exceptions, commented).
- **Checkpoint (task 3):** human-verified all 8 steps live — merged notepad rows, Enter launch, Ctrl+Enter Explorer reveal with file selected, Ctrl+Shift+Enter silent-normal, folder glyph + Explorer open, Add executable… persistence to `%APPDATA%\TID\wisp\wisp.ini`, indexer-disabled status row (Stop-Service WSearch) with apps still working, and typing feel (instant apps, ~150ms quiet fill-in, no stale rows).
- **Checkpoint note fix:** "vibrant left selection-bar indicator" — left-edge accentLight bar on `ListView.isCurrentItem`, width bumped to Theme.spaceSm (8px) so it reads clearly against the accent selection background.
- **Verification state at completion:** build clean; ctest 13/13 (tst_shell, hotkey, launcher, capture, matcher, model, enum, catalog, launch, tray, search, filesearch, history); locked status copy single-homed in src/core/FileSearch.cpp (absent from qml/); startup smoke test alive 5s, no crash.

## task Commits

Each task was committed atomically:

1. **task 1: Wire FileSearch + LaunchHistory in main.cpp** - `94bcf20` (feat)
2. **task 2: QML UX — status row, Ctrl+Enter reveal, folder glyph, Add executable row** - `7ee201c` (feat)
3. **task 3: Human verification of the live file-search experience** - checkpoint (approved with note)
4. **Checkpoint note: vibrant left selection-bar indicator** - `5e4b76e` (feat)

**Plan metadata:** final docs commit (docs: complete wiring + QML plan)

## Files Created/Modified

- `src/app/main.cpp` - FileSearch/LaunchHistory construction + all five seam assignments + fileSearch context property + resultsReady→setFileResults connect (52 insertions)
- `qml/MainWindow.qml` - dual-pipeline onTextChanged, Ctrl-only reveal branch, statusRow overlay, emptyState gate, pinned addExeRow (75 insertions / 9 deletions across both QML files)
- `qml/ResultsRow.qml` - D-04 folder ▸ glyph + accentLight tint, checkpoint-note selection indicator bar

## Decisions Made

- Selection indicator width = Theme.spaceSm (8px), not spaceXs (4px): the lighter accentLight bar sits on the already-blue Theme.accent selection background; 8px is clearly visible while still a "small bar" (checkpoint note wording). Token-only, no literals.
- Branch order in launchFromKey stays elevated (Ctrl+Shift) → reveal (Ctrl) → normal — byte-identical with the 03-04 policy except the new Ctrl branch; reveal is a designed no-op for Lnk/Uwp rows (04-03 policy), so Ctrl+Enter on an app row changes nothing.
- Status row is a full-list-area overlay (anchors.fill: resultsView) rather than a model row — non-selectable, unfocusable, un-keyboardable per D-18; emptyState defers to it only when the list is empty.

## Deviations from Plan

None - plan executed exactly as written (2/2 auto tasks; the checkpoint was the planned blocking human gate).

### Checkpoint Note (user-requested, post-approval)

The human checkpoint was **approved with a note**: the selected row needed a visible indicator ("a small bar at the left side of the item that is selected with a vibrant color that is easily seen"). The orchestrator drafted the initial implementation (4px accentLight bar); the executor reviewed it, bumped the width to Theme.spaceSm (8px) for "easily seen" contrast against the accent background, re-verified build + full ctest (13/13), and committed as `5e4b76e`.

**Total deviations:** 0 auto-fixed (Rules 1-3 not triggered)
**Impact on plan:** The note fix adds a small visual element beyond the plan's files list (same file set: ResultsRow.qml); no scope creep, no contract changes.

## Issues Encountered

None - both auto tasks completed first-pass; build and all suites green throughout. The checkpoint itself was the only pause, by design.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Phase 4 is complete: 5/5 plans, 16/16 total plans across the milestone, 13/13 ctest suites, human-verified vertical slice (8/8 steps + approved note). Ready for Phase 5 (theme & visual polish): the folder ▸ glyph and monogram initial are the documented swap points for real icons; MatchRangesRole (03) + the now-approved selection indicator pre-validate the accent visuals; the icon pipeline (IShellItemImageFactory) and mixed-DPI spike are the known Phase-5 research items. Phase 6 (tray/settings/autostart) consumes the settings store.

---

*Phase: 04-file-search*
*Completed: 2026-08-10*
