# Phase 5: Theme & Visual Polish - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-10
**Phase:** 5-Theme & Visual Polish
**Areas discussed:** Icon pipeline & caching, Match highlighting style, Theme depth & selection treatment, Accent system pre-wiring

---

## Icon pipeline & caching

| Option | Description | Selected |
|--------|-------------|----------|
| 32px (Recommended) | Matches the existing 32px monogram placeholder — zero layout change, best look at 44px rows, one GetImage call per icon. Slightly more cache memory. | ✓ |
| 24px | Compact PowerToys-Run-like density; leaves room for a second text line later, but requires a layout change. | |
| 16px | Smallest; Rofi-ish density, but icons look tiny in the current row design. | |
| QQuickImageProvider (Recommended) | STACK's recommended path: icons decode off the UI thread, Image.source = "image://wisp/icons/{id}", QML handles load-async + placeholder. | ✓ |
| Model role with QImage | Simpler wiring, but images cross threads/serialize per refresh and rows re-fetch on every filter keystroke. | |
| Direct file path | Works only for plain file icons; .lnk/UWP-style icons need Win32 extraction Qt can't do. | |
| Bounded LRU, memory only (Recommended) | Capped in-memory cache (~500 32px icons ≈ 5-10 MB), LRU eviction; extraction is cheap so re-extract on eviction is fine. | ✓ |
| LRU + disk persistence | Survives restarts, but adds disk format/invalidation/cleanup code for sub-10ms re-extraction. | |
| Unbounded | Simplest, but violates "cache without unbounded growth" criterion. | |
| Monogram, crossfade to icon (Recommended) | Monogram (first letter) shows instantly, crossfades to the real icon — feels instant, reuses Phase 3 placeholder. | ✓ |
| Generic glyph | Uniform look but loses per-app letter identity during load. | |
| Blank while loading | Cleanest visually, but rows look empty for slow cold-UWP cases. | |

**User's choice:** All recommended options; 32px / QQuickImageProvider / bounded LRU memory-only / monogram crossfade.
**Notes:** None.

---

## Match highlighting style

| Option | Description | Selected |
|--------|-------------|----------|
| accentLight (Recommended) | Bright blue pops on dark surface; keeps selection and match text as distinct visual layers. | |
| Accent exact | Consistent, but lower contrast on dark surfaces; reads mid-tone rather than vibrant. | |
| accentLight + selected-remap | Fluent-style: match = accentLight, swapped for readability when the row is selected (accent bg). | ✓ |
| Color only (Recommended) | Just the color swap; cleanest, zero reflow. | |
| Color + bold | Stronger Rofi feel, but mixed weights cause subtle width jumps. | |
| Color + background chip | Rounded background chip behind each matched run — most prominent, busier. | ✓ |
| Title line only (Recommended) | Only the result title highlighted; the secondary line is metadata, chip treatment on paths gets noisy. | ✓ |
| Title + secondary line | Consistent, but path characters are common — typing 'e' lights up half the path. | |
| White + darker accent chip (Recommended) | On the accent selection bg, matched runs become white text on a slightly darker accent chip — still distinguishable. | ✓ |
| Bold white, no chip | Cleanest on the accent bar, but highlight identity lost while navigating. | |
| Inverted chip | Most contrast, but looks foreign next to the flat accent bar. | |

**User's choice:** accentLight + selected-remap; color + background chip; title line only; white + darker accent chip.
**Notes:** User prefers prominent, vibrant emphasis — selected the strongest treatments.

---

## Theme depth & selection treatment

| Option | Description | Selected |
|--------|-------------|----------|
| Keep + tune (Recommended) | The Phase 4 treatment (accent bg + vibrant left bar) becomes the polished final form; tune tokens only. | ✓ |
| Left bar only | More minimal/Rofi-like, but loses the strong "selected" affordance. | |
| Add entrance animation | Accent bg + left bar plus scale/glow pulse — risks feeling gimmicky in a 150ms-fast launcher. | |
| Full set (Recommended) | Add hover bg, pressed bg, placeholder color, styled scrollbar, designed empty state — all visible in normal use. | ✓ |
| Essentials only | Only hover + placeholder; scrollbar and empty state stay unstyled (default QML look peeks through). | |
| Full + decorative | Plus glow/gradients/custom cursor — adds render cost against the 60fps bar. | |
| Centered text + glyph (Recommended) | Quiet message in textSecondary with a small glyph; doesn't compete with typing. | ✓ |
| Status row only | Simplest, but emptiness reads as a bug to new users. | |
| Suggestions | "Did you mean…" — requires matcher-side logic, belongs elsewhere. | |
| Auto-hide overlay (Recommended) | Hidden normally, fades in on scroll/hover, styled dark with theme tokens. | ✓ |
| Always visible | Communicates list length, but adds permanent chrome to an ephemeral popup. | |
| None | Cleanest; mouse users lose position feedback on long lists (empty query shows every app). | |

**User's choice:** Keep + tune; full token set; centered text + glyph; auto-hide overlay scrollbar.
**Notes:** None.

---

## Accent system pre-wiring

| Option | Description | Selected |
|--------|-------------|----------|
| Settings-backed, startup read (Recommended) | Theme.qml reads accent from the settings store at startup (default #0078D4); Phase 6's picker only writes one value and notifies. | ✓ |
| Hardcoded for now | Least Phase 5 code, but every QML file needs re-binding in Phase 6. | |
| Full live-ready | Live notification plumbing now — dead code until the picker exists. | |
| SettingsStore class (Recommended) | Small C++/QSettings class owned by the controller, readAccent()/setAccent(); testable, reused by Phase 6. | ✓ |
| Inline QSettings in main | Fewer files, but untestable logic in app wiring; Phase 6 extracts it anyway. | |
| QML-side read | Zero C++, but mixes config I/O into presentation; untestable. | |
| Derived at runtime (Recommended) | Theme.qml computes accentDark/accentLight via QColor adjustments from one stored accent — any picked color yields consistent shades. | ✓ |
| Fixed tokens | Predictable per default color, but mismatched for custom accents. | |
| Luminance-derived | Guaranteed contrast for any color, but adds colorimetry code for shades that only need to look right. | |
| Silent default (Recommended) | Missing/corrupt accent → silently use #0078D4; a launcher must open instantly, cosmetic value. | ✓ |
| Default + warn log | Silent to users, visible in dev builds. | |
| Tray notification | Visible, but a fresh install (no settings file) would nag on first run. | |

**User's choice:** Settings-backed startup read; SettingsStore class; derived at runtime; silent default.
**Notes:** None.

---

## OpenCode's Discretion

- Worker-thread extraction design (QtConcurrent/QThread per ARCHITECTURE), exact LRU implementation, QQuickImageProvider registration details.
- UWP logo resolution implementation (indirect strings + scale variants — hard requirement, planner's call on how).
- Icon extraction failure fallback, HBITMAP → QImage conversion path, chip radius/padding values.
- Empty-state copy text, Theme.qml token names, exact derived-shade algorithms.

## Deferred Ideas

None — discussion stayed within phase scope. v2 exclusions reaffirmed: backdrop blur (VISU-04) and accent picker (VISU-03) stay in Phase 6; empty-state suggestions explicitly rejected.
