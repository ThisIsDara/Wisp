pragma Singleton
import QtQuick

QtObject {
    // --- Color (UI-SPEC Color section) ---
    readonly property color surface: "#000000"          // dominant 60% — window surface (fully black, 2026-08-15)
    readonly property color surfaceSecondary: "#2D2D30" // secondary 30% — inset wells (Phase 3)
    // Neon/cyberpunk identity (2026-08-15; Phase-11 accent-follow 2026-09-02):
    // appOutline is the bright neon ring around the WHOLE app; appOutlineDim is
    // the dim halo ring just outside it (reads as a neon tube with zero blur
    // cost — the hot path is never blurred; listBg is the results panel,
    // DARKER than surface). Phase-11 D-01/D-02: the outline now FOLLOWS the
    // user-selected accent — no orange survives a non-orange accent; every
    // consumer edits nothing because they reference the Token.
    // 2026-09-02 FIX (accent-follow bug): appOutline is the accent ITSELF, NOT
    // Qt.lighter(accent,1.5). Qt.lighter MULTIPLIES each RGB channel and CLAMPS
    // to 255, so a saturated mid-tone accent washed to cream — #F0883E × 1.5
    // → #FFCC5D (the red channel pinned at 255 and green lifted) — "way whiter
    // than it should be" next to the old solid-orange ring. The accent is
    // already the saturated identity color; the ring needs it undiluted.
    readonly property color listBg: "#000000"             // results panel — fully black, matches the surface (2026-08-15)
    readonly property color appOutline: accent // bright neon outline around the whole app — the EXACT picked accent (Phase-11 D-02, fixed 2026-09-02)
    readonly property color appOutlineDim: Qt.darker(appOutline, 1.6) // dim halo ring outside the bright ring — derives from appOutline (darkens the accent ≈ old #FF7A00→#9C5400 ratio)
    readonly property int appOutlineWidth: 2              // neon outline border width
    // Element separators (2026-08-15): hairline dividers between rows/sections
    // are the SAME color as the neon outline — the user's "make all the
    // separating lines match the outline" ask. Kept as a named token so the
    // dividers stay in lockstep with the identity color (which Phase-11 made
    // accent-derived — so separators follow the accent too).
    readonly property color separator: appOutline
    readonly property color border: "#3F3F46"           // 1px surface hairline
    // D-13/D-15/D-16: the SINGLE mutable color source. The initializer IS the
    // silent fallback — missing/corrupt wisp.ini accent resolves to this
    // default with no toast (SettingsStore guarantees it too). MainWindow's
    // Component.onCompleted writes the stored value once at startup; Phase 6's
    // picker writes reactively via Connections onAccentChanged. Never assigned
    // here — this QML never parses the INI (T-05-21).
    property color accent: "#0078D4"
    // D-15 derivation contract: accentLight/accentDark are DERIVED from
    // accent, never hand-picked — any Phase-6 picked accent yields consistent
    // shades by construction. accentLight targets ≈ #58A6FF at the default
    // accent (UI-SPEC line 95); the 1.45 factor tunes ONLY at visual
    // checkpoint 8 against the 3-4 probe accents (contrast guard: accentLight
    // on surface ≥ 4.5:1 and white on accentDark ≥ 4.5:1).
    readonly property color accentLight: Qt.lighter(accent, 1.45) // match text / selection bar / ▸ glyph
    readonly property color accentDark: Qt.darker(accent, 1.4)    // selected-row chip bg (D-06)
    // On-accent text that stays legible across the full pickable accent set
    // AND custom colors (Phase-11 2026-09-02). The All/Favorites toggle active
    // segments fill with appOutline (= accent); hard black on a DARK accent is
    // unreadable. Rule: black when the accent's WCAG relative luminance is
    // clearly bright (L > 0.2 — preserves the black-on-bright cyberpunk look),
    // else white. Threshold 0.2: borderline mid-tones (violet #8E5CF7 sits
    // right at the boundary, L≈0.20) err toward the safe white side. Relative
    // luminance = sRGB linearization + Rec.709 weights (WCAG 2.x); baked = a
    // function reading `accent` so the binding re-runs on every picker delta
    // (same reactive contract as appOutline / chipBg).
    function accentRelLum() {
        function linsrgb(c) { return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4) }
        return 0.2126 * linsrgb(accent.r) + 0.7152 * linsrgb(accent.g) + 0.0722 * linsrgb(accent.b)
    }
    property color onAccentAdaptive: accentRelLum() > 0.2 ? surface : onAccentText
    readonly property color textPrimary: "#F5F5F5"
    readonly property color textSecondary: "#A0A0A0"

    // --- Phase-5 interaction tokens (D-10, UI-SPEC Color) ---
    readonly property color hoverBg: "#2D2D30"            // hover row bg — dedicated token (= surfaceSecondary)
    readonly property color pressedBg: Qt.darker(surfaceSecondary, 1.15) // pressed bg — derived ≈ #26262A, transient
    readonly property color placeholderColor: "#A0A0A0"   // search-field placeholder (= textSecondary)
    readonly property color scrollbarThumb: "#3F3F46"     // overlay thumb, rest state (border)
    readonly property color scrollbarThumbHover: "#A0A0A0" // thumb while hovering the list (textSecondary)
    readonly property color scrollbarTrack: "transparent" // overlay — no track, no layout space (D-12)
    readonly property color emptyStateGlyphColor: "#A0A0A0" // empty-state glyph (D-11, textSecondary)
    readonly property real disabledOpacity: 0.45   // 05.1: hidden rows in show-hidden mode (dim to mute, still readable)
    readonly property real fullOpacity: 1.0        // 05.1: visible-row opacity (zero-literal rule)
    readonly property int menuZ: 20                // 05.1: context menu stacking (above list + dismissal catcher)
    readonly property int catcherZ: 15             // 05.1: dismissal catcher stacking (below menu, above list)

    // Unselected-chip bg (UI-SPEC line 109): accent at ~20% alpha BLENDED OVER
    // the surface → opaque by construction. NOT Qt.rgba over accentLight, NOT
    // semi-transparent — a translucent chip would bleed the window surface
    // through and grey the highlight. The binding re-runs on accent change so
    // the Phase-6 picker path keeps chips consistent (D-15).
    function blendOverSurface(alpha) {
        var a = accent
        var s = surface
        return Qt.rgba(a.r * alpha + s.r * (1 - alpha),
                       a.g * alpha + s.g * (1 - alpha),
                       a.b * alpha + s.b * (1 - alpha), 1.0)
    }
    readonly property color chipBg: blendOverSurface(0.2)

    // --- Spacing (4px grid, UI-SPEC Spacing Scale) ---
    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 12
    readonly property int spaceLg: 16
    readonly property int spaceXl: 24
    readonly property int space2xl: 32
    readonly property int space3xl: 48
    readonly property int space4xl: 64
    readonly property int rowHeight: 44                  // declared exception (Phase 3 rows)
    readonly property int radiusSurface: 0               // declared exception — sharp corner (cyberpunk / neon redesign)

    // --- Phase-5 spacing tokens (declared sub-grid exceptions — UI-SPEC Spacing) ---
    readonly property int iconSize: 32            // icon slot (D-01) — same slot as the 32px monogram
    readonly property int chipRadius: 4           // rounded highlight chip corner (D-07)
    readonly property int chipPadX: 4             // chip horizontal padding (spaceXs)
    readonly property int chipHeight: 20          // chip height, centered on the title line box
    readonly property int removeButtonSize: 24   // hover-revealed row remove button (declared 4-grid)
    readonly property int removeButtonRadius: 12 // fully rounded circle (declared sub-grid garnish)
    readonly property int scrollbarWidth: 6       // overlay thumb width (declared 6px exception)
    readonly property int scrollbarInset: 2       // thumb inset from the list's right edge (overlay)
    readonly property int scrollbarRadius: 3      // thumb corner radius
    readonly property int emptyStateGlyphSize: 16 // empty-state glyph (UI-SPEC Typography rule 4: fontSizeSubtitle × 4/3)
    readonly property int emptyStateWellSize: 48  // empty-state glyph well (48px, 4-grid) — the composed well
    readonly property int emptyStateWellRadius: 24 // fully rounded well (declared sub-grid garnish)
    readonly property int emptyStateWellGlyphSize: 20 // glyph inside the well (declared between 15/18)
    readonly property int searchUnderlineHeight: 2 // search-field focus bar (declared sub-grid — Windows 11 focus underline)
    // 2026-08-15: indeterminate scan-progress bar (track = surfaceSecondary,
    // moving chunk = appOutline)
    readonly property int scanBarHeight: 4     // progress-bar track height (declared sub-grid)
    readonly property int scanBarRadius: 2     // fully rounded ends (declared sub-grid garnish)
    readonly property int tickWidth: 5            // sliding selection tick width (2026-08-11 redesign)
    readonly property int tickHeight: 26          // tick height — compact, centered on the 44px row
    readonly property int tickRadius: 3           // tick corner radius (fully rounded ends)
    readonly property int menuWidth: 120          // right-click context menu width (05.1)
    readonly property int menuRadius: 6           // context menu corner radius (05.1)
    readonly property int menuItemHeight: 28      // compact context-menu item height (05.1)
    readonly property int fontSizeMenu: 13        // context menu item text (05.1, declared between 12/15)

    // --- Phase-6/7 settings tokens (06-UI-SPEC Spacing Scale / Color / Geometry) ---
    // Window + surface geometry (480x360 → 480x560 + scan section, 07-05;
    // surface = window − 2x16 shadow margin, same shell as the launcher).
readonly property int settingsWindowWidth: 480
readonly property int settingsWindowHeight: 756   // right-aligned scan/updates actions + breathing room: rows shrink, then scan roots grew to 36px rows (was 740)
readonly property int settingsSurfaceWidth: 448
readonly property int settingsSurfaceHeight: 724  // window − 2x16 shadow margin (was 708)
    readonly property int colorDialogWindowWidth: 280
    readonly property int colorDialogWindowHeight: 320
    readonly property int colorDialogSurfaceWidth: 248
    readonly property int colorDialogSurfaceHeight: 288
    // Shortcuts reference window (Phase 12): a header + description rows.
    // Sized so 11 description rows (42px each) fit the surface WITHOUT a
    // scrollbar (research: no clipped list, no scroll chrome). Window =
    // surface + 2x16 shadow margin.
    readonly property int shortcutsWindowWidth: 560
    readonly property int shortcutsWindowHeight: 580
    readonly property int shortcutsSurfaceWidth: 528
    readonly property int shortcutsSurfaceHeight: 548
    readonly property int shortcutRowHeight: 42
    readonly property int shortcutRowGap: 2
    // Accent picker swatches (D-05/D-08: 9 curated mid-tone colors, LOCKED
    // order — 06-UI-SPEC palette table; the picker renders this array, never
    // literals). Derived accentDark contrast for the cyan entry (#00B7C3) at
    // factor 1.4 computes ≈ 4.6:1 ≥ 4.5:1 — the D-15 clamp guard does not
    // trigger, so the derivation factors above stay unchanged (2026-08-11).
    readonly property var accentSwatches: [ "#0078D4", "#00B7C3", "#2EA043", "#F14E4E", "#F0883E", "#C99A2E", "#8E5CF7", "#D669B8", "#5C6BC0" ]
    readonly property int swatchSize: 24
    readonly property int swatchGap: 8
    readonly property int swatchRingSize: 28      // selection-ring footprint (24 + 2x2 ring)
    readonly property int ringWidth: 2            // declared sub-grid garnish (selection indicator)
    readonly property int swatchRadius: 6         // declared sub-grid exception (tokenized well corners)
    readonly property color swatchWellBg: "#2D2D30" // strip backing / unselected swatch well (= surfaceSecondary)
    // Fields/rows (UI-SPEC Spacing Scale; row heights are 4-grid: 64/88/64/158)
    readonly property int fieldRadius: 6          // hotkey value well corner (tokenizes shipped capture-dialog literal)
    readonly property int fieldHeight: 36         // hotkey value well height (tokenizes shipped capture-dialog literal)
    readonly property int stepperSize: 24         // interval ± chip footprint (= swatchSize, declared 4-grid)
    readonly property int settingsRowHotkey: 64
    readonly property int settingsRowAccent: 88
    readonly property int settingsRowAutostart: 64
    readonly property int settingsRowGap: 12      // spaceMd — declared (not 16; vertical budget 488 <= 528)
    readonly property int settingsPad: 24         // content column margins (spaceXl)
    // "Scan locations" section (07-06, header-on-top polish): header(32) +
    // breathing gap(12) + roots(72: 2 roomy 36px rows so the Remove buttons
    // clear each other and the hairline) + gap(4) + interval(28) + gap(4) +
    // action(28) + bottom pad(8) = 188 exact.
    readonly property int settingsRowScan: 188
    readonly property int settingsRowScanItem: 28 // interval row / action row height
    readonly property int settingsRowScanRoot: 36 // per-root row — 24px Remove button + 6px clearance top/bottom
    readonly property int settingsRowScanRoots: 72 // visible root-list height (2 rows of 36)
    readonly property int settingsSectionHeader: 32 // section header (18 title + 2 + 12 subtitle)
    // Phase 8 Updates section (header-on-top polish, right-aligned check
    // actions): header(32) + gap(8) + toggle row(28) + gap(4) + check row(40:
    // status+hint stacked left, buttons right) + gap(4) + download bar(0..6)
    // + bottom pad(8) = 124..130 within 132.
    readonly property int settingsRowUpdates: 132   // right-aligned layout (was 160)
    readonly property int settingsRowUpdatesCheck: 40 // status(20) + hint(16) stacked, buttons centered beside
    // Phase 12 Show-shortcuts row: 64px (hotkey-row family) — opens ShortcutsWindow.
    readonly property int settingsRowShortcuts: 64
    // Phase 8 update dialog (UI-SPEC S2 geometry).
    // Prompt inner budget: title(20)+gap(8)+subcopy(2x16)+gap(8)+buttons(28)
    // = 96 <= 136 (window - 2x16 shadow/surface margins). UAT fix: 132 was
    // ~30px short - buttons landed outside the window.
    readonly property int updateDialogWidth: 336
    readonly property int updateDialogHeight: 168
    // Autostart toggle (UI-SPEC Color interaction tokens)
    readonly property int toggleWidth: 40
    readonly property int toggleHeight: 20
    readonly property int knobSize: 16            // track − 2x2 inset
    readonly property int toggleTrackRadius: 10   // fully rounded pill (declared sub-grid garnish)
    readonly property int knobRadius: 8           // fully rounded (declared sub-grid garnish)
    readonly property color toggleTrackOff: "#3F3F46" // off state (= border)
    readonly property color toggleTrackOn: accent     // on state — BINDING to accent, never a literal
    readonly property color knobColor: "#F5F5F5"      // knob (= textPrimary)
    // Custom color dialog (06-UI-SPEC Geometry)
    readonly property int svSize: 160             // SV square (and hue bar length)
    readonly property int hueBarWidth: 24
    readonly property int hueBarRadius: 6
    // Danger — transient validation/rejection text ONLY (06-UI-SPEC Color;
    // ≈4.3:1 on surface declared acceptable for transient text). Tokenizes the
    // shipped Phase-2 capture-dialog literal.
    readonly property color danger: "#E5484D"
    // D-09 destructive-button affordance tokens (Phase 12): hover on a remove
    // button turns text + border danger-red so the action reads as destructive.
    readonly property color dangerText: danger
    readonly property color dangerBorder: danger
    // Primary-button text on accent fill (declared exception — the custom
    // dialog OK button). Tokenizes the shipped capture-dialog OK-button
    // literal so that file's zero-hex gate holds; pixel-identical.
    readonly property color onAccentText: "#FFFFFF"

    // --- Selection motion (2026-08-11 user redesign) ---
    // Sliding tick, row scale-up, and row/chip color transitions all share
    // this 140ms OutCubic token — short enough to feel instant under the
    // keyboard, long enough to read.
    readonly property int animNav: 140
    // Selected-row emphasis (2026-08-11): the current row grows slightly
    // inside the list — scale from the row center, never geometry (no reflow).
    readonly property real selectedScale: 1.04

    // --- Typography (UI-SPEC Typography) ---
    readonly property string fontFamily: "Segoe UI Variable"
    readonly property string fontFamilyFallback: "Segoe UI"
    readonly property int fontSizeQuery: 18
    readonly property int fontSizeTitle: 15
    readonly property int fontSizeSubtitle: 12
    readonly property int fontSizeKeycap: 12
    readonly property int fontWeightRegular: 400
    readonly property int fontWeightSemibold: 600

    // --- Animation (UI-SPEC Animation & Motion Contract — VISU-01) ---
    readonly property int animOpenDuration: 150
    readonly property int animCloseDuration: 140
    readonly property int easingOpen: Easing.OutCubic
    readonly property int easingClose: Easing.InCubic
    readonly property real animScaleFrom: 0.96
    readonly property real animScaleTo: 1.0
    // Phase-5 micro-animations (UI-SPEC Animation table): icon crossfade AND
    // scrollbar overlay fade share this 120ms token — opacity only, Linear.
    readonly property int animFade: 120

    // --- Window geometry (UI-SPEC Geometry contract) ---
    readonly property int surfaceWidth: 648
    readonly property int surfaceHeight: 440
    readonly property int shadowMargin: 16                // window canvas = surface + 2×margin
    readonly property int windowWidth: surfaceWidth + shadowMargin * 2   // 680
    readonly property int windowHeight: surfaceHeight + shadowMargin * 2 // 432

    // --- Shadow (UI-SPEC Color) ---
    readonly property real shadowOpacity: 0.45            // black ~45%
}
