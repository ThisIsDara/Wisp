---
phase: 06-tray-settings-autostart-packaging
plan: 06-02
subsystem: qml-surface
tags: [qml, qmlcachegen, theme-tokens, settings-window, color-dialog, tokenization, cmake]

# Dependency graph
requires:
  - phase: 05-theme-visual-polish
    provides: Theme.qml token singleton + SettingsStore accent store shape (D-13..D-16)
  - phase: 06-tray-settings-autostart-packaging (06-01)
    provides: AutostartManager Run-key store (06-02 toggle binds to its controller surface in 06-03)
provides:
  - Theme.qml Phase-6 extension: 35 new tokens (geometry, swatches, rows, toggle, hue bar, danger, onAccentText)
  - SettingsWindow.qml: the locked 480x360 settings surface (hotkey well, 9-swatch accent strip + ring, custom entry, autostart toggle)
  - ColorDialog.qml: 280x320 ApplicationModal staged color dialog (SV square, hue bar, hex readout, OK/Cancel)
  - HotkeyCaptureDialog.qml literal debt paid: zero hex literals, radius/height tokenized, zero behavior change
  - CMakeLists.txt: 2 new QML files registered in qt_add_qml_module
affects: [06-03, 06-04, 06-05]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Theme.qml as the ONLY literal carrier: new files reach zero hex literals; the picker renders the accentSwatches array token, never its own colors"
    - "Controller injection contract: `readonly property var settingsController: null` + per-instance beginCreate/setProperty (capture-dialog precedent) — QML stays loadable and bindings re-resolve on set"
    - "Staged dialog state: HSV staging with generation of the hex string from staged values (never freeform input, T-06-01); re-seed from Theme.accent on every open"
    - "Ring-follows-accent binding (Qt.colorEqual scan of accentSwatches) + keyboardSwatch override for staged keyboard nav — ring snaps, never animates"
    - "Nested-item centering in Row/Column: wrapper Items with fixed ring-height + anchors.centerIn (Row layout overrides child anchors)"

key-files:
  created:
    - qml/SettingsWindow.qml
    - qml/ColorDialog.qml
  modified:
    - qml/Theme.qml
    - qml/HotkeyCaptureDialog.qml
    - CMakeLists.txt

key-decisions:
  - "D-15 clamp guard verified numerically, no change: cyan #00B7C3 derived accentDark at factor 1.4 computes ≈4.6:1 ≥ 4.5:1 — derivation factors stay untouched (noted in Theme.qml comment)"
  - "Added ONE extra token beyond the plan's list — onAccentText: '#FFFFFF' — because the plan's own automated gate (zero hex literals in HotkeyCaptureDialog.qml) conflicts with its 'no other edits' clause: the shipped OK-button white text is a hex literal. Pixel-identical token, documented as a deviation"
  - "ColorDialog bottom bar combines preview+hex readout (left) with OK/Cancel (right) in one 32px row — the only layout honoring the locked margins-16 + 288px surface height (stacked rows need 278 > 256 available)"
  - "Hue bar height uses Theme.svSize (160) — plan tokenizes width only; no new token needed"
  - "Swatch click applies immediately (live apply, D-06); keyboard arrows stage a ring position that Enter/Space then applies"

requirements-completed: [SYS-03, VISU-03]

# Metrics
duration: 40min
completed: 2026-08-11
---

# Phase 06 Plan 02: Settings Surfaces — Theme Tokens, SettingsWindow, ColorDialog Summary

**The complete settings visual surface in token-only QML: 35 new Theme.qml tokens (incl. the locked 9-swatch accent palette and danger), the 480x360 SettingsWindow implementing the full UI-SPEC contract (hotkey well, accent strip with selection ring + keyboard nav, autostart toggle), the staged 280x320 custom ColorDialog (SV square, hue bar, hex readout, OK/Cancel), and the shipped HotkeyCaptureDialog literal debt paid to zero — all four QML files compiling clean through qmlcachegen with 19/19 tests still green.**

## Performance

- **Duration:** 40 min
- **Started:** 2026-08-11T16:59:30Z
- **Completed:** 2026-08-11T21:39:38Z
- **Tasks:** 3
- **Files modified:** 5 (2 created QML, 2 modified QML, 1 modified CMakeLists)

## Accomplishments

- **Theme.qml (+35 tokens):** Phase-6 token set — settings/color-dialog window+surface geometry (480x360/448x328, 280x320/248x288), the locked 9-value `accentSwatches` array (exact UI-SPEC order), swatch strip tokens (size/gap/ring/ringWidth/radius/wellBg), row tokens (fieldRadius/fieldHeight, row heights 64/88/64, rowGap 12, pad 24), toggle tokens (40x20 track, 16 knob, `toggleTrackOn: accent` as a live BINDING — never a literal), hue-bar tokens, `danger: "#E5484D"`, and `onAccentText: "#FFFFFF"` (deviation, below). D-15 guard verified numerically: cyan accentDark ≈ 4.6:1 ≥ 4.5:1 → no clamp of the derivation factors.
- **HotkeyCaptureDialog.qml tokenized with zero behavior change (D-03):** `#E5484D` → `Theme.danger`, well radius 6 → `Theme.fieldRadius`, well height 36 → `Theme.fieldHeight`, OK-button `#FFFFFF` → `Theme.onAccentText`. File now has **zero hex literals** (grep-verified) — the UI-SPEC line-17 debt paid.
- **SettingsWindow.qml (480x360, Tool|FramelessWindowHint):** static-shadow shell (MainWindow family, surface 448x328 radius 12 + 1px border + shadow.png at 0.45). Content column fits the declared 318 ≤ 328 budget: "Settings" header (15/600), then three rows separated by 1px hairlines — Hotkey (well: 36px/radius 6, Keycap 12/600 elided value bound to `settingsController.currentHotkey`, click + Enter/Space → `hotkeyRowClicked()`, hoverBg hover), Accent color (label + 9-swatch strip from `Theme.accentSwatches`, 28px ring footprints with 2px accentLight selection ring, click → `applyAccent`, ←/→ stages the ring, Enter/Space applies, "Custom…" text button → `openColorDialog()`), Start with Windows (description copy verbatim, 40x20 toggle with `toggleTrackOn` accent binding, 16px knob sliding 120ms via `Behavior on x`, click/Space → `toggleAutostart()`). Tab order: hotkey well → strip → Custom… → toggle. Zero hex literals, zero inline `component`.
- **ColorDialog.qml (280x320, Qt.ApplicationModal):** staged color surface (D-07/D-06) — SV square 160x160 (two-stop hue gradient + transparent→black overlay, crosshair drag → stageSat/stageVal), 24x160 rainbow hue bar (drag → stageHue), live 32x32 preview + hex readout generated from staged HSV (`hsvToHex`, never freeform — T-06-01), OK (accent fill + `onAccentText` white, 90x32) / Cancel (surfaceSecondary) at capture-dialog geometry, Enter confirms / Esc discards, re-seeds from `Theme.accent.hsvHue/Saturation/Value` on every open.
- **CMakeLists.txt:** `SettingsWindow.qml` + `ColorDialog.qml` registered in `qt_add_qml_module` QML_FILES.
- **Wave gate:** full configure + build (qmlcachegen compiled all 4 QML files, wisp.exe linked) + **19/19 ctest green**.

## task Commits

Each task was committed atomically:

1. **task 1: Extend Theme.qml tokens + tokenize HotkeyCaptureDialog.qml literals** - `47a737f` (feat)
2. **task 2: SettingsWindow.qml — the locked 480x360 settings surface** - `b8e3419` (feat)
3. **task 3: ColorDialog.qml + CMakeLists QML_FILES registration + build gate** - `a1413aa` (feat)

## Files Created/Modified

- `qml/Theme.qml` - Phase-6 token block: 8 geometry, 7 swatch + palette array, 7 field/row, 8 toggle, 3 hue-bar, danger, onAccentText tokens; D-15 guard note (cyan ≈4.6:1, no clamp)
- `qml/SettingsWindow.qml` - NEW — full locked settings surface; controller contract: currentHotkey/autostartEnabled props, applyAccent/toggleAutostart/openColorDialog calls, hotkeyRowClicked signal
- `qml/ColorDialog.qml` - NEW — staged ApplicationModal color dialog; commitCustomColor contract; HSV staging with hex generation
- `qml/HotkeyCaptureDialog.qml` - 4 literal → token replacements only (Theme.danger/fieldRadius/fieldHeight/onAccentText); zero hex literals, zero behavior change
- `CMakeLists.txt` - 2 QML_FILES entries added

## Decisions Made

- D-15 clamp guard verified numerically (cyan accentDark ≈ 4.6:1 ≥ 4.5:1) — no derivation-factor change, documented in Theme.qml.
- Controller injection reuses the capture-dialog pattern: `readonly property var settingsController: null` + null-guarded calls; 06-03 injects via beginCreate/setProperty and bindings re-resolve on set.
- ColorDialog bottom bar merges preview/hex and OK/Cancel into one 32px row to honor the locked margins-16 + 288px surface (stacked rows would exceed by 22px).
- Keyboard swatch selection is staged (arrows move the ring) while mouse clicks apply live — both families per UI-SPEC Interaction contract.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Plan's "zero hex literals" gate conflicts with "No other edits to HotkeyCaptureDialog.qml"**
- **Found during:** task 1 verification
- **Issue:** The plan's automated gate (`grep -cE "#[0-9A-Fa-f]{6}" == 0` in HotkeyCaptureDialog.qml) can never pass: the shipped OK button's white text is a literal `#FFFFFF` (line 182), which the plan's three named tokenizations (`#E5484D`, radius 6, height 36) don't cover — while the action clause forbids any other edit.
- **Fix:** Added ONE token to Theme.qml — `onAccentText: "#FFFFFF"` (documented as the primary-button-on-accent-fill text; UI-SPEC's declared OK-button exception) — and replaced the literal with it. Pixel-identical, zero behavior change; the gate now passes and ColorDialog's OK button reuses the same token.
- **Files modified:** qml/Theme.qml, qml/HotkeyCaptureDialog.qml
- **Verification:** zero-hex grep on HotkeyCaptureDialog.qml = 0; build + tests green
- **Committed in:** 47a737f (task 1 commit)

**2. [Rule 3 - Blocking] Plan's Theme token list omitted two tokens its own interfaces referenced**
- **Found during:** task 1 (Theme extension)
- **Issue:** The plan's `<interfaces>` section lists `fieldRadius` (6) and `fieldHeight` (36) as existing Theme tokens, but they do not exist in the shipped Theme.qml — while task 1's "Fields/rows" list and task 3's button/well geometry require them.
- **Fix:** Added both tokens as specified in the task-1 list (they were plainly intended additions, not new deviations). No other action needed.
- **Files modified:** qml/Theme.qml
- **Committed in:** 47a737f (task 1 commit)

---

**Total deviations:** 2 auto-fixed (1 Rule 1 plan-spec conflict, 1 Rule 3 missing-token gap)
**Impact on plan:** Both fixes were required for the plan's own acceptance gates to pass; no scope creep, no behavior change.

## Issues Encountered

- None. The build environment (vcvars64 via build.ps1 pattern) worked cleanly; no running wisp.exe held the output file this time.

## User Setup Required

None - pure QML/CMake plan; the controller that instantiates these surfaces arrives in 06-03.

## Next Phase Readiness

- **06-03 (tray + settings controller):** ready to consume `SettingsWindow.qml` + `ColorDialog.qml` via the documented injection contract — `readonly property var settingsController: null`, injected per-instance with beginCreate/setProperty; Q_INVOKABLE surface expected: `currentHotkey()` (Q_PROPERTY + NOTIFY), `applyAccent(QColor)`, `openColorDialog()`, `commitCustomColor(QString hex)`, `toggleAutostart()`, `autostartEnabled` (Q_PROPERTY + NOTIFY). HotkeyCaptureDialog remains byte-identical in behavior for the hotkey-row click path.
- **CMakeLists.txt:** both new files registered; qmlcachegen compiles them; nothing else to wire.
- No blockers.

## Known Stubs

None. `settingsController` defaults to null with safe fallbacks ("Alt+Space" hotkey, autostart off, no-op calls) until 06-03 injects the real controller — that is the documented injection contract (same as `dialogHost` in the shipped capture dialog), not a stub.

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: none | — | No new surface beyond the plan's register: no network/auth/file access added; the hex readout is generated from staged HSV (T-06-01 mitigated as planned); bounded 9-swatch input path (T-06-04 accepted as planned). |

---

*Phase: 06-tray-settings-autostart-packaging*
*Completed: 2026-08-11*

## Self-Check: PASSED

All 5 modified/created files verified on disk (Theme.qml, SettingsWindow.qml, ColorDialog.qml, HotkeyCaptureDialog.qml, CMakeLists.txt); all 3 task commits (47a737f, b8e3419, a1413aa) verified in git log; wave gate: configure + build + 19/19 ctest green.
