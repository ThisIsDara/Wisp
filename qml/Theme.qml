pragma Singleton
import QtQuick

QtObject {
    // --- Color (UI-SPEC Color section) ---
    readonly property color surface: "#1E1E1E"          // dominant 60% — window surface
    readonly property color surfaceSecondary: "#2D2D30" // secondary 30% — inset wells (Phase 3)
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
    readonly property int radiusSurface: 12              // declared exception — window corner radius

    // --- Phase-5 spacing tokens (declared sub-grid exceptions — UI-SPEC Spacing) ---
    readonly property int iconSize: 32            // icon slot (D-01) — same slot as the 32px monogram
    readonly property int chipRadius: 4           // rounded highlight chip corner (D-07)
    readonly property int chipPadX: 4             // chip horizontal padding (spaceXs)
    readonly property int chipHeight: 20          // chip height, centered on the title line box
    readonly property int scrollbarWidth: 6       // overlay thumb width (declared 6px exception)
    readonly property int scrollbarInset: 2       // thumb inset from the list's right edge (overlay)
    readonly property int scrollbarRadius: 3      // thumb corner radius
    readonly property int emptyStateGlyphSize: 16 // empty-state glyph (UI-SPEC Typography rule 4: fontSizeSubtitle × 4/3)
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
    readonly property int settingsWindowHeight: 560
    readonly property int settingsSurfaceWidth: 448
    readonly property int settingsSurfaceHeight: 528
    readonly property int colorDialogWindowWidth: 280
    readonly property int colorDialogWindowHeight: 320
    readonly property int colorDialogSurfaceWidth: 248
    readonly property int colorDialogSurfaceHeight: 288
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
    readonly property int settingsRowHotkey: 64
    readonly property int settingsRowAccent: 88
    readonly property int settingsRowAutostart: 64
    readonly property int settingsRowGap: 12      // spaceMd — declared (not 16; vertical budget 488 <= 528)
    readonly property int settingsPad: 24         // content column margins (spaceXl)
    // "Scan locations" section (07-05, D-10): 18 title + 8 + 56 roots + 8 +
    // 28 interval + 8 + 28 action = 154 within 158.
    readonly property int settingsRowScan: 158
    readonly property int settingsRowScanItem: 28 // per-root row / interval row / action row height
    readonly property int settingsRowScanRoots: 56 // visible root-list height (2 rows of 28)
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
    readonly property int surfaceHeight: 400
    readonly property int shadowMargin: 16                // window canvas = surface + 2×margin
    readonly property int windowWidth: surfaceWidth + shadowMargin * 2   // 680
    readonly property int windowHeight: surfaceHeight + shadowMargin * 2 // 432

    // --- Shadow (UI-SPEC Color) ---
    readonly property real shadowOpacity: 0.45            // black ~45%
}
