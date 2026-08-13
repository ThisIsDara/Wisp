# Phase 5: Theme & Visual Polish - Context

**Gathered:** 2026-08-10
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 5 makes the launcher look like a product: a complete sleek dark theme (VISU-02), accent-colored selection and match highlighting (LAUN-06), and real icons loaded asynchronously with bounded caching — the polish that makes the "instant and smooth" feel visible. The accent value lives in the settings store, pre-wired for Phase 6's live picker (VISU-03). Backdrop blur is v2 (VISU-04) and is NOT in this phase.

**Success criteria (ROADMAP):** (1) dark surfaces, readable contrast, rounded corners, no light-mode leftovers; (2) matched characters highlighted in accent color using ranker match positions from Phase 3; (3) selected result visually distinct with accent color; (4) app and file results show correct icons, loaded asynchronously with no UI freeze; UWP icons resolve properly (indirect strings + scale variants) and cache without unbounded growth.

**Requirement:** VISU-02, LAUN-06. **Not in scope:** accent picker (Phase 6), backdrop blur (v2 VISU-04), settings window (Phase 6), recency (v2).

</domain>

<decisions>
## Implementation Decisions

### Icon Pipeline & Caching
- **D-01:** Icons render at **32px** — matches the existing 32px monogram placeholder and 44px row; no layout change.
- **D-02:** Extracted icons reach QML via a **QQuickImageProvider** (`image://wispicons/{id}`) — off-thread decode, Image.source-based async loading; the STACK-recommended path. Not a model role, not direct file paths.
- **D-03:** Cache is **bounded in-memory LRU** (~500 32px icons ≈ 5-10 MB), no disk persistence. Extraction is sub-10ms via `IShellItemImageFactory`, so re-extract on eviction is fine. Satisfies "cache without unbounded growth".
- **D-04:** While an icon is loading, the row shows the **existing monogram placeholder, crossfading to the real icon** when extraction lands. Reuses the Phase 3 placeholder.

### Match Highlighting Style (LAUN-06)
- **D-05:** Highlight color is **accentLight (#58A6FF)** — vibrant on the dark surface, distinct from the accent selection layer.
- **D-06:** **Selected-row remap**: on the accent selection background, matched runs render **white text on a darker accent chip** so highlights stay readable.
- **D-07:** Emphasis is **color + rounded background chip** behind each matched run (strong, Rofi-style) — not just color or weight.
- **D-08:** Highlighting applies to the **title line only**; the secondary metadata line (folder path / package name) is never highlighted.
- Highlight data source is already shipped: FuzzyMatcher → ResultsModel `MatchRangesRole` (Phase 3 contract "03-05"). This phase is pure rendering.

### Theme Depth & Selection Treatment
- **D-09:** The Phase 4 selection treatment (**accent background + vibrant left-edge bar**) is the final form — this phase tunes tokens only (radius, bar width, hover complement). No rework, no entrance animation.
- **D-10:** Full token set added to Theme.qml: **hover bg, pressed bg, search-placeholder color, styled scrollbar, designed empty state**. No decorative extras (glow, gradients) — 60fps bar wins.
- **D-11:** Empty state = **centered short message in textSecondary with a small glyph** (e.g., "No results for 'xyz'"). No suggestions logic.
- **D-12:** Scrollbar = **auto-hide overlay** styled with theme tokens (border/thumb); visible while scrolling / hovering the list.

### Accent System Pre-Wiring (Phase 6 readiness)
- **D-13:** Accent is read from the **settings store at startup** (default `#0078D4`); all accent usages (selection bg, left bar, highlight, chips) bind to it. Phase 6's picker writes one value and notifies.
- **D-14:** Introduce a **SettingsStore class** (C++/QSettings over `%APPDATA%\TID\wisp\wisp.ini`) owned by the controller, with `readAccent()`/`setAccent()` — unit-testable, reused by Phase 6 (accent picker, autostart toggle). No inline QSettings in main, no QML-side config.
- **D-15:** Accent variants are **derived at runtime**: Theme.qml computes accentDark (selected-remap chip) and accentLight (highlight) from the stored accent via QColor adjustments — one source of truth, so any Phase 6 picked color yields consistent shades.
- **D-16:** Missing/corrupt accent setting → **silent fallback to #0078D4**. No toasts, no tray notifications.

### OpenCode's Discretion
- Worker-thread extraction design (QtConcurrent/QThread per ARCHITECTURE), exact LRU implementation, QQuickImageProvider registration, UWP logo resolution details (indirect strings + scale variants — mandated by criterion 4 but implementation is planner's call), icon extraction failure fallback (e.g., re-extract on demand), HBITMAP → QImage conversion path, empty-state copy text, chip radius/padding values, Theme.qml token names and exact derived-shade algorithms — planner's call within architectural conventions (ARCHITECTURE.md, STACK.md).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase Contract & Scope
- `.planning/ROADMAP.md` — Phase 5 goal, 4 success criteria (incl. UWP indirect strings + scale variants, bounded cache), design note that the accent value lives in the settings store.
- `.planning/REQUIREMENTS.md` — VISU-02 (dark theme), LAUN-06 (accent-colored match highlighting) requirement text; LAUN-06 marked Phase 5.

### Prior Phase Contracts (consume, don't re-discuss)
- `.planning/phases/01-core-shell/01-UI-SPEC.md` — Approved shell design: 640×400 fixed, 44px rows, 4px grid, 12px radius, Theme.qml token singleton — Phase 5 must never introduce literal values.
- `.planning/phases/03-app-search-result-model-app-catalog/03-CONTEXT.md` — D-07 match-ranges contract ("03-05" highlight data), monogram placeholder decision (D-05 "no icon pipeline this phase — Phase 5"), iconRef field contract.
- `.planning/phases/04-file-search/04-CONTEXT.md` — iconRef populated for file results (`iconPath;index`), MatchRangesRole shape, ResultsRow.qml current state (32px monogram, folder ▸ glyph, left-bar selection indicator).
- `.planning/STATE.md` — Locked decisions: Qt 6.11.1, settings INI at `%APPDATA%\TID\wisp\wisp.ini`, controller-owned visibility, token-only styling.

### Research (locked stack & Windows APIs)
- `.planning/research/STACK.md` — Icon map (HIGH confidence): `SHCreateItemFromParsingName` → `IShellItemImageFactory::GetImage(32, SIIGBF_ICONONLY|SIIGBF_RESIZETOFIT)`; UWP via `AppListEntry.DisplayInfo.GetLogo()`; **never call on the UI thread without `SIIGBF_INCACHEONLY`** — worker thread + cache; HBITMAP → QImage → QPixmap → QQuickImageProvider or model role; SHGetFileInfo is legacy fallback only; QSettings registry vs INI notes.

### Code Contract References
- `qml/Theme.qml` — existing token singleton this phase extends (token-only rule).
- `src/core/ResultsModel.{h,cpp}` — MatchRangesRole ("03-05 Phase-5 highlight contract") consumed by the highlight renderer.
- `src/core/AppEntry.h` — iconRef field ("Phase 5 consumes it"); format `iconPath;index` from WinStartMenuEnumerator, logo ref for UWP.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `qml/Theme.qml` — token singleton: colors, 44px row, 12px radius, 4px spacing grid, typography, 150/140ms anim durations — the highlight/selection/scrollbar styling extends from these tokens.
- `qml/ResultsRow.qml` — 32px monogram placeholder (radius Theme.radiusSurface), accent-bg + left-bar selection (Phase 4), secondary line, folder ▸ glyph (U+25B8) — icon swap + highlight rendering slot into this file.
- `src/core/ResultsModel.{h,cpp}` — MatchRangesRole already emits `QVariantList` of start/length ranges per result ("03-05 Phase-5 highlight contract") — renderer consumes, no matcher changes.
- `src/core/AppEntry.h` + `src/win/WinStartMenuEnumerator.cpp` + `src/win/WinUwpEnumerator.{h,cpp}` — iconRef (`iconPath;index` / logo ref) ready for the extraction pipeline.
- `src/tray/TrayIcon.cpp` — precedent for programmatically generated icons with no asset dependency; same approach could back the provider's fallback.
- `src/core/LaunchHistory.{h,cpp}` — QSettings-based store precedent (WR-01 fix added QMutex) for the SettingsStore class pattern.

### Established Patterns
- Token-only styling (01-UI-SPEC): QML never introduces literal colors/spacing — all new elements add Theme.qml tokens first.
- Windows detail behind `src/win/` firewall with pure C++ interfaces for testability (WinHotkey/WinFullscreenGuard precedent).
- Worker-thread + QtConcurrent/QThread + mutex-protected shared state is the established pattern (catalog, file search, LaunchHistory).
- `wisp_core` static lib + `tests/tst_*` Qt-Test pattern (13 green) — icon extraction, LRU cache, and SettingsStore get their own tst with the same wiring.

### Integration Points
- `src/app/main.cpp` — SettingsStore instantiation + QQuickImageProvider registration on the engine; controller wires settings to Theme.
- `qml/MainWindow.qml` — empty-state and scrollbar styling live in the results area; theme tokens consumed throughout.
- `src/core/` — new: IconProvider/IconCache (worker-thread extraction), SettingsStore; Theme.qml binding to the accent value.

</code_context>

<specifics>
## Specific Ideas

- User wants the launcher to feel "vibrant" and "easily seen" (Phase 4 selection-bar note) — carried into D-05 (accentLight highlight) and D-07 (background chips).
- User accepted all recommended options without freeform additions; discussion stayed fast and decisive — all four areas were pre-identified from the phase goal, no new gray areas surfaced.
- Success criterion 4 explicitly requires UWP icons resolve properly (indirect strings + scale variants) — treated as a hard requirement, not a nice-to-have.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope. Existing v2 exclusions reaffirmed: backdrop blur (VISU-04) and accent picker (VISU-03) stay in Phase 6; empty-state suggestions ("Did you mean…") explicitly rejected — would require matcher-side logic.

</deferred>

---

*Phase: 5-Theme & Visual Polish*
*Context gathered: 2026-08-10*
