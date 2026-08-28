import QtQuick
import wisp

// 44px result-row delegate (01-UI-SPEC Results List contract, Phase 3).
// ALL visuals via Theme tokens — zero literals (hard rule). Selection truth
// lives in ResultsModel: ListView.isCurrentItem mirrors the model's
// selectedIndex (bound in MainWindow.qml), so keyboard moves (moveSelection)
// and clicks (selectIndex) render through the SAME accent highlight.
// Hover NEVER selects (2026-08-11) — passive hoverBg/pressedBg cue only, and
// never paints accent (UI-SPEC rule).
// The delegate takes no focus and steals no keys: the shell owns every key
// (LAUN-05 keyboard contract).
Item {
    id: row
    width: ListView.view.width
    implicitHeight: Theme.rowHeight
    // 2026-08-15: row hover INCLUDING the remove button AND the favorite star
    // — both sit on top of hoverArea (they consume their own presses), so
    // without this a cursor on either would blank the row's hover bg (Qt Quick
    // delivers hover to the topmost MouseArea only). Drives the hover bg, the
    // buttons' visibility, and the text column's rightMargin.
    property bool hovered: hoverArea.containsMouse || removeHover.containsMouse || favHover.containsMouse

    // Selected-row emphasis (2026-08-11 user redesign): the current row grows
    // slightly. GROW FROM THE LEFT EDGE (origin Left) — a center origin shifts
    // the row's left half outside the list clip, sliding the icon column under
    // the selection tick (observed 2026-08-11). Left-anchored growth bulges
    // right into the scrollbar gutter — invisible, nothing moves.
    scale: ListView.isCurrentItem ? Theme.selectedScale : 1.0
    transformOrigin: Item.Left
    Behavior on scale { NumberAnimation { duration: Theme.animNav; easing.type: Theme.easingOpen } }

    // 05.1: rule- or user-hidden rows render dimmed in show-hidden mode —
    // one binding, zero literals.
    opacity: model.isHidden ? Theme.disabledOpacity : Theme.fullOpacity

    // Selection / hover / pressed background (accent = current; hoverBg for
    // hover; pressedBg while mouse-down — transient, sub-100ms; transparent
    // otherwise). Hover NEVER paints accent (UI-SPEC rule) — D-10 tokens.
    // ColorAnimation so the selection change reads as motion, not a jump.
    Rectangle {
        anchors.fill: parent
        color: ListView.isCurrentItem ? Theme.accent
             : hoverArea.pressed ? Theme.pressedBg
             : row.hovered ? Theme.hoverBg
             : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animNav; easing.type: Theme.easingOpen } }
    }

    // Selection indicator (2026-08-11 redesign): the accent tick now lives on
    // the ListView highlight (MainWindow.qml) so it SLIDES between rows —
    // the static per-row bar was removed. Rows paint only the accent bg.

    // 32px monogram placeholder (D-04): the fallback layer while the real
    // icon loads — or permanently when extraction fails (D-16 silent, null
    // QImage → Image.Error → opacity stays 1). Folder rows swap the initial
    // for the ▸ marker glyph (U+25B8 — the one declared literal) in accent
    // tint; the folder's real icon arrives via the provider like any other
    // row (iconRef 'path:path' — WinIconExtractor handles shell items).
    // Crossfade (D-04, UI-SPEC 120ms): placeholder fades OUT as the icon
    // fades IN on Image.Ready — no flash on warm cache (status check is
    // reactive either way; both layers animate opacity only, never geometry).
    Rectangle {
        id: monogram
        anchors.left: parent.left
        anchors.leftMargin: Theme.spaceLg   // 16px from the list edge — keeps the icon column optically identical after the 2026-08-11 list-margin change
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.space2xl          // 32
        height: Theme.space2xl
        radius: Theme.radiusSurface
        color: Theme.surfaceSecondary
        opacity: iconImg.status === Image.Ready ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.animFade } }
        Text {
            anchors.centerIn: parent
            text: model.iconKey === "calc" ? "\uE8EF" : (model.isFolder ? "\u25B8" : (model.displayName.length > 0 ? model.displayName.charAt(0).toUpperCase() : ""))
            font.family: model.iconKey === "calc" ? "Segoe MDL2 Assets" : ""
            font.pixelSize: model.iconKey === "calc" ? 24 : Theme.fontSizeSubtitle
            font.weight: Theme.fontWeightSemibold
            color: model.iconKey === "calc" ? Theme.appOutline : (model.isFolder ? Theme.accentLight : Theme.textSecondary)
        }
    }

    // Real icon layer (D-01/D-02/D-04, 05-04 contract): image://wispicons/{id}
    // served by the provider on Qt's dedicated provider thread. cache: false
    // is MANDATORY — QPixmapCache double-caching would unboundedly defeat the
    // IconCache LRU (research Pitfall 2). encodeURIComponent is the locked
    // id round-trip (Pitfall 4; provider decodes defensively, T-05-19).
    Image {
        id: iconImg
        anchors.fill: monogram             // 32px slot (D-01)
        source: "image://wispicons/" + encodeURIComponent(model.iconKey)
        sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: status === Image.Ready
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.animFade } }
    }

    // Title + subtitle column (single-line, elide right — UI-SPEC Typography).
    Column {
        anchors.left: monogram.right
        anchors.leftMargin: Theme.spaceMd
        anchors.right: parent.right
        // 2026-08-15: while hovered the remove button AND the favorite star occupy
        // the right ~64px — the title shortens so it never runs under them.
        // A favorited-but-not-hovered row still reserves the star's slot.
        anchors.rightMargin: row.hovered
                             ? Theme.spaceSm + Theme.removeButtonSize + Theme.spaceXs
                               + Theme.removeButtonSize + Theme.spaceSm
                             : (model.isFavorite
                                ? Theme.spaceSm + Theme.removeButtonSize + Theme.spaceXs
                                : Theme.spaceMd)
        anchors.verticalCenter: parent.verticalCenter

        // Title line box (LAUN-06, D-05..D-08): the ONLY line that carries
        // match highlighting (subtitle below is never highlighted). Rich-text
        // color spans + a metric-positioned rounded-chip layer behind
        // (research Pattern 3 — Qt's HTML subset has NO border-radius, so the
        // roundness comes from the Rectangles; rich text also disables
        // Text.elide, hence the FontMetrics manual elision below).
        Item {
            id: titleLine
            width: parent.width
            height: titleText.height

            // Manual elide (UI-SPEC Typography rule 2): the elided string is
            // the single source for BOTH the HTML and the chip geometry — runs
            // clamp against it, so chips can never drift past the elide
            // boundary. `font` MUST mirror the rendered title Text /exactly/
            // (research Pitfall 6 — subpixel drift).
            FontMetrics {
                id: fm
                font: titleText.font
                property string elided: elidedText(model.displayName, Qt.ElideRight, titleLine.width)
            }

            // Chip geometry: x/width per matched run, advanceWidth-positioned
            // under the spans (Pattern 3). Rebuilds ONLY on width / model-data
            // / selection change — the binding re-runs when fm.elided or
            // model.matchRanges change; colors are separate bindings below.
            function computeChips() {
                var chips = []
                var ranges = model.matchRanges || []
                var elided = fm.elided
                var elidedLen = elided.length
                for (var i = 0; i < ranges.length; i++) {
                    var start = ranges[i][0]
                    var len = ranges[i][1]
                    if (start >= elidedLen) continue          // run past elide boundary — drop
                    var end = Math.min(start + len, elidedLen) // crossing run — trim
                    var runText = elided.substring(start, end)
                    chips.push({
                        x: fm.advanceWidth(elided.substring(0, start)) - Theme.chipPadX,
                        width: fm.advanceWidth(runText) + 2 * Theme.chipPadX
                    })
                }
                return chips
            }
            property var chipRuns: computeChips()

            Repeater {
                model: titleLine.chipRuns
                Rectangle {
                    x: modelData.x
                    y: (titleLine.height - Theme.chipHeight) / 2   // vertically centered on the line box
                    width: modelData.width
                    height: Theme.chipHeight
                    radius: Theme.chipRadius
                    // D-06 remap: accentDark chip on the accent selection bg
                    // (white span text on top — see buildTitleHtml); unselected
                    // = opaque accent-tinted chipBg (UI-SPEC line 109). Color
                    // animates with the selection motion (animNav).
                    color: ListView.isCurrentItem ? Theme.accentDark : Theme.chipBg
                    Behavior on color { ColorAnimation { duration: Theme.animNav; easing.type: Theme.easingOpen } }
                }
            }

            Text {
                id: titleText
                width: parent.width

                // T-05-18: displayName flows into rich-text HTML — every
                // segment is escaped before span-wrapping (Qt's HTML subset is
                // script-free, and the escape makes injection inert regardless).
                // NOTE: Qt.escape() (Qt 5) was removed in Qt 6 — the plan's
                // documented alternative ("Qt.escape() or replace &,<,>") is
                // the local replace-based escape below.
                function escapeText(s) {
                    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                }

                // Rebuilds ONLY on width/data/selection change (binding deps:
                // fm.elided, model.matchRanges, ListView.isCurrentItem) —
                // never per-frame (UI-SPEC hard rule 2).
                function buildTitleHtml() {
                    var ranges = model.matchRanges || []
                    var elided = fm.elided
                    var selected = ListView.isCurrentItem
                    var html = ""
                    var pos = 0
                    for (var i = 0; i < ranges.length; i++) {
                        var start = ranges[i][0]
                        var len = ranges[i][1]
                        if (start >= elided.length) continue
                        var end = Math.min(start + len, elided.length)
                        html += escapeText(elided.substring(pos, start))
                        // D-05/D-06: unselected = accentLight text on chipBg;
                        // selected = white (textPrimary) on accentDark.
                        html += "<span style=\"color:" + (selected ? Theme.textPrimary : Theme.accentLight)
                              + ";background-color:" + (selected ? Theme.accentDark : Theme.chipBg) + "\">"
                              + escapeText(elided.substring(start, end)) + "</span>"
                        pos = end
                    }
                    html += escapeText(elided.substring(pos))
                    return html
                }
                text: buildTitleHtml()
                textFormat: Text.RichText
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightRegular
                color: Theme.textPrimary      // unmatched segments (near-white on accent: 4.7:1, UI-SPEC)
            }
        }
        }

    // 05.1: right-click curation request — the shell owns the actual menu
    // (in-window overlay in MainWindow.qml, concrete widgets). Coordinates
    // are mapped into the ListView space so the shell can position the
    // overlay without touching delegate transforms (delegates scale 1.04
    // when current — a delegate-scoped Popup landed wrong / never showed;
    // observed 2026-08-11).
    signal contextMenuRequested(int index, bool isHidden, bool isFavorite, real x, real y)

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Selection ownership (2026-08-11 user change): hover NEVER moves the
        // selection — the tick stays put under the cursor; ↑/↓/Page/Home/End
        // move it, and a CLICK selects the clicked row then launches it.
        // Hover still paints hoverBg/pressedBg as a passive cue only.
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                // 05.1: right-click selects the row (tick sync) and asks the
                // shell to open the curation context menu (CUR-02/03).
                resultsModel.selectIndex(model.index)
                var p = row.mapToItem(row.ListView.view, mouse.x, mouse.y)
                contextMenuRequested(model.index, model.isHidden, model.isFavorite, p.x, p.y)
                return
            }
            resultsModel.selectIndex(model.index) // visual sync — tick slides to the click
            launchController.launchIndex(model.index, false) // LAUN-05: click launches (D-12 freeze in C++)
        }
    }

    // ── Hover-revealed favorite star (2026-08-15) ──
    // Toggles the row's favorite flag via the model (favoriteSelected/
    // unfavoriteSelected — id-based, so file rows favorite too). Always visible
    // on a favorited row (orange filled star); hover-revealed otherwise (the
    // outline star). Mirrors the remove button's counter-scale x pin: sits
    // just LEFT of the remove button, visually right-pinned across the scaled
    // selected row. Consumes its own presses (never launches); right-clicks
    // fall through (LeftButton only) so the context menu still works.
    Item {
        id: favBtn
        visible: model.iconKey !== "calc"
        width: Theme.removeButtonSize
        height: Theme.removeButtonSize
        x: (parent.width - Theme.spaceSm) / row.scale - Theme.removeButtonSize
           - Theme.spaceXs - width
        anchors.verticalCenter: parent.verticalCenter
        opacity: model.isFavorite ? Theme.fullOpacity : (row.hovered ? Theme.fullOpacity : 0)
        Behavior on opacity { NumberAnimation { duration: Theme.animFade } }
        Text {
            anchors.centerIn: parent
            text: model.isFavorite ? "\uE735" : "\uE734" // MDL2 FavoriteStarFill / FavoriteStar — the one declared literal
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: Theme.fontSizeSubtitle
            color: model.isFavorite ? Theme.appOutline : Theme.textSecondary
        }
        MouseArea {
            id: favHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                resultsModel.selectIndex(model.index)
                if (model.isFavorite)
                    resultsModel.unfavoriteSelected()
                else
                    resultsModel.favoriteSelected()
            }
        }
    }

    // ── Hover-revealed remove button (2026-08-15) ──
    // Removes the row via the SAME CUR-02 machinery as right-click → Hide and
    // Ctrl+H: select the row, then toggle hidden/unhidden (show-hidden mode →
    // restore). Renders only on hideable rows (model.isHideable — CUR-04
    // parity, so a transient index file row never gets a dead button). Fades
    // in/out with the shared 120ms opacity-only micro-animation contract; sits
    // ON TOP of hoverArea and consumes its own presses — clicking it can never
    // launch. Right-clicks fall through to hoverArea (only LeftButton
    // accepted), so the context menu still works over the button.
    Item {
        id: removeBtn
        visible: model.isHideable
        width: Theme.removeButtonSize
        height: Theme.removeButtonSize
        // 2026-08-15: the selected row scales up (selectedScale, from the
        // LEFT edge), which would push a right-anchored button ~4% right on
        // the always-selected first row — "the X is a bit off for the first
        // item". Instead of anchors.right, pin the VISUAL right edge to the
        // row's logical right edge by dividing the logical x by row.scale:
        // rendered right = (x + width) * row.scale = parent.width - spaceSm,
        // constant across every row (selected or not).
        x: (parent.width - Theme.spaceSm) / row.scale - width
        anchors.verticalCenter: parent.verticalCenter
        opacity: row.hovered ? Theme.fullOpacity : 0
        Behavior on opacity { NumberAnimation { duration: Theme.animFade } }
        Rectangle {
            anchors.fill: parent
            radius: Theme.removeButtonRadius
            // Current-row (accent bg) → accentDark fill, accentLight ring
            // (focus family); otherwise a surfaceSecondary well with border,
            // pressedBg on hover — the same well vocabulary as the steppers.
            color: ListView.isCurrentItem
                 ? Theme.accentDark
                 : removeHover.containsMouse ? Theme.pressedBg : Theme.surfaceSecondary
            border.width: 1
            border.color: ListView.isCurrentItem ? Theme.accentLight : Theme.border
        }
        Text {
            anchors.centerIn: parent
            text: "\uE711" // MDL2 "Cancel" (X) — the one declared literal
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: Theme.fontSizeSubtitle
            color: ListView.isCurrentItem || removeHover.containsMouse ? Theme.textPrimary : Theme.textSecondary
        }
        MouseArea {
            id: removeHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                resultsModel.selectIndex(model.index)
                if (model.isHidden)
                    resultsModel.unhideSelected()
                else
                    resultsModel.hideSelected()
            }
        }
    }
}