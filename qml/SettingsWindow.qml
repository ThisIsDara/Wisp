import QtQuick
import QtQuick.Controls
import QtQuick.Window
import wisp

// SYS-03 / D-01/D-02: the settings surface — a dedicated dark tool window,
// 480x756 (token-only, D-08; 864→740 right-aligned exact-fit, then +16
// for roomy 36px scan-root rows).
// 06-03 controller is injected as `settingsController` on open (per-instance
// beginCreate/setProperty — HotkeyCaptureDialog precedent). Every value is
// read/written through it; this window never parses the INI.
//
// Dismissal (Esc / click-away with launcher exemption) and the 120ms open
// fade are controller-owned (06-03, D-02/D-04) — this surface only renders
// and signals. No close animation, no scale (UI-SPEC Animation contract).
Window {
    id: root
    title: "wisp — settings"          // a11y / taskbar identity (UI-SPEC Copywriting)
    flags: Qt.Tool | Qt.FramelessWindowHint
    // 2026-09-15 (screen-fit): the surface keeps its exact token geometry and
    // scales as ONE unit when the screen is too short for it (1366x768 leaves
    // ~728px usable vs this window's 756px — it used to be placed at a
    // negative y and clipped off the top). Exactly 1 on any roomier screen.
    //
    // 2026-10-08: the window now GROWS with the folders list instead of
    // scrolling it. `foldersExtra` is how far the wrapping chip rows exceed the
    // folders space the base window already carries (Theme.settingsRowScanFolders,
    // 72px — previously two fixed 36px root rows), so a typical folder list
    // leaves the window at exactly its old 813px and only a genuinely tall list
    // makes it taller. fitScale then shrinks the whole unit to fit the screen,
    // the same mechanism a short display already used: nothing scrolls, nothing
    // is hidden.
    readonly property int foldersExtra: Math.max(0, foldersArea.height - Theme.settingsRowScanFolders)
    readonly property int winHeight: Theme.settingsWindowHeight + foldersExtra
    readonly property int surfaceHeight: Theme.settingsSurfaceHeight + foldersExtra
    readonly property real uiScale: Theme.fitScale(Theme.settingsWindowWidth, winHeight)
    width: Math.round(Theme.settingsWindowWidth * uiScale)
    height: Math.round(winHeight * uiScale)
    color: "transparent"
    visible: false   // resident — the controller shows it (06-03)

    // 06-03 injects the controller via beginCreate/setProperty; the null-safe
    // guards below keep the surface loadable, and bindings re-resolve on set.
    // NOT readonly (2026-08-12): setProperty on a readonly QML property is a
    // silent no-op — the injection never landed, every row call was dead.
    property var settingsController: null

    // Hotkey well value (Keycap 12/600) — controller-supplied, elided on
    // overflow (UI-SPEC text rule 3), never reflowed.
    property string currentHotkey: settingsController ? settingsController.currentHotkey : "Alt+Space"
    // Autostart state — the controller refreshes it on window open (D-10).
    property bool autostartEnabled: settingsController ? settingsController.autostartEnabled : false

    // The swatch whose color equals the current accent; -1 when the accent is
    // custom (no ring). Re-evaluates live as Theme.accent changes (D-06).
    property int selectedSwatch: {
        for (var i = 0; i < Theme.accentSwatches.length; ++i)
            if (Qt.colorEqual(Theme.accentSwatches[i], Theme.accent))
                return i
        return -1
    }
    // Keyboard-staged selection (UI-SPEC Interaction): arrows move this,
    // Enter/Space applies it. -1 = no staged selection — the ring follows
    // the live accent. Snap behavior (ring moves, no apply) matches the
    // staged-contract family; hover never moves the ring.
    property int keyboardSwatch: -1
    property int ringIndex: keyboardSwatch >= 0 ? keyboardSwatch : selectedSwatch

    // 2026-08-12 (CR-01): the old flow emitted a hotkeyRowClicked signal
    // with NO handler anywhere — clicking the row did nothing. Now the row
    // calls the controller directly, exactly like applyAccent/
    // toggleAutostart/openColorDialog below (the injected host null-guards
    // every call).
    function openHotkeyCapture() {
        if (settingsController)
            settingsController.openHotkeyCapture()
    }

    // Centered on primary screen (capture-dialog geometry logic); the
    // controller re-centers on every open (UI-SPEC Geometry contract).
    function centerOnScreen() {
        const aw = Screen.availableWidth
        const ah = Screen.availableHeight
        if (!isFinite(aw) || !isFinite(ah))
            return   // no screen yet — the onUiScaleChanged pass re-runs this
        x = Screen.availableX + Math.round((aw - width) / 2)
        y = Screen.availableY + Math.round((ah - height) / 2)
    }
    Component.onCompleted: centerOnScreen()
    // 2026-09-15: Screen.* is undefined until the window is actually attached
    // to a screen, so uiScale is still 1 when onCompleted runs and the window
    // only resizes a moment later — without this the surface stays offset by
    // the height delta and reads as mis-centered. Re-centering when the scale
    // settles keeps it honest.
    onUiScaleChanged: centerOnScreen()

    function toggleAutostart() {
        if (settingsController)
            settingsController.toggleAutostart()
    }
    function applySwatch(i) {
        keyboardSwatch = -1
        if (settingsController)
            settingsController.applyAccent(Theme.accentSwatches[i])
    }
    function moveSwatch(delta) {
        var cur = keyboardSwatch >= 0 ? keyboardSwatch : selectedSwatch
        cur = cur < 0 ? 0 : cur
        keyboardSwatch = Math.max(0, Math.min(Theme.accentSwatches.length - 1, cur + delta))
    }
    function commitSwatch() {
        if (keyboardSwatch >= 0)
            applySwatch(keyboardSwatch)
        keyboardSwatch = -1
    }
    function openColorDialog() {
        if (settingsController)
            settingsController.openColorDialog()
    }

    // Phase 12: Show shortcuts row → controller bridge (openHotkeyCapture
    // pattern — the QML surface signals, the injected host null-guards).
    function openShortcuts() {
        if (settingsController)
            settingsController.openShortcuts()
    }

    // Folder-chip label (2026-10-08): the folder's own name, not its full path,
    // so a chip stays compact enough to wrap several to a line. Handles both
    // separator flavours (roots are normalised to native, but a hand-edited INI
    // or a future '/' producer shouldn't blank the chip) and a trailing
    // separator ("C:\Games\" -> "Games", not "").
    function chipFolderName(path) {
        if (!path)
            return ""
        const trimmed = String(path).replace(/[\\/]+$/, "")
        const cut = Math.max(trimmed.lastIndexOf("\\"), trimmed.lastIndexOf("/"))
        return cut >= 0 ? trimmed.slice(cut + 1) : trimmed
    }

    // Static pre-rendered shadow — same shell as MainWindow (assets/shadow.png,
    // 16px margin, opacity 0.45). Explicit natural size + TopLeft origin so it
    // tracks the surface under the screen-fit scale (anchors.fill would
    // measure the ALREADY-scaled window and re-apply the factor).
    Image {
        x: 0
        y: 0
        width: Theme.settingsWindowWidth
        height: root.winHeight
        scale: root.uiScale
        transformOrigin: Item.TopLeft
        source: "assets/shadow.png"
        opacity: Theme.shadowOpacity
    }

    // The surface (448x724 + 2x16 shadow margin inside the 480x756 window).
    // Same TopLeft-scaled treatment as the shadow above, so the 16px margin
    // stays even when the whole surface is scaled down on a short screen.
    // 2026-10-08: both heights track root.winHeight/surfaceHeight, which grow
    // with the folders chips.
    Rectangle {
        id: surface
        x: (Theme.settingsWindowWidth - Theme.settingsSurfaceWidth) / 2
        y: (root.winHeight - root.surfaceHeight) / 2
        width: Theme.settingsSurfaceWidth
        height: root.surfaceHeight
        scale: root.uiScale
        transformOrigin: Item.TopLeft
        radius: Theme.radiusSurface
        color: Theme.surface
        border.color: Theme.border
        border.width: 1
        clip: true

        // Drag header — entire top bar drags the frameless window (Qt's
        // startSystemMove). Close button sits on top so its clicks don't drag.
        MouseArea {
            id: dragArea
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: closeBtn.left
            anchors.rightMargin: Theme.spaceSm
            height: 32
            onPressed: (mouse) => root.startSystemMove()
            // Title inside the drag area so it's not covered
            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.settingsPad
                anchors.top: parent.top
                anchors.topMargin: 10
                text: "Settings"
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightSemibold
                color: Theme.textPrimary
            }
        }

        // X close button — top-right of the surface, frameless window's
        // only chrome. Hover → surfaceSecondary well, same as row remove.
        Item {
            id: closeBtn
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: Theme.spaceSm
            anchors.rightMargin: Theme.spaceSm
            width: Theme.removeButtonSize
            height: Theme.removeButtonSize
            Rectangle {
                anchors.fill: parent
                radius: Theme.removeButtonRadius
                color: closeHover.containsMouse ? Theme.surfaceSecondary : "transparent"
                border.width: 1
                border.color: closeHover.containsMouse ? Theme.border : "transparent"
            }
            Text {
                anchors.centerIn: parent
                text: "\uE711" // MDL2 Cancel (X)
                font.family: "Segoe MDL2 Assets"
                font.pixelSize: Theme.fontSizeSubtitle
                color: closeHover.containsMouse ? Theme.textPrimary : Theme.textSecondary
            }
            MouseArea {
                id: closeHover
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    if (settingsController)
                        settingsController.close()
                }
            }
        }

        // Content column — five labelled sections (2026-09-15 regroup). Every
        // simple row follows ONE pattern (label + sub-label left, control
        // right); the two blocks that hold several controls use a header line
        // with their action pinned right and the controls below. Spacing is
        // settingsRowGap INSIDE a section and settingsSectionGap BETWEEN them,
        // so the grouping is legible without adding boxes or extra chrome.
        // Budget: 8 top + 602 rows + 56 section gaps + 24 bottom = 690 of the
        // 692 available (724 surface − 32 drag header) — still an exact fit.
        Column {
            id: contentColumn
            // objectName mirrors the id so tests can locate this exact column
            // (findChild resolves objectName, not a QML id).
            objectName: "settingsContentColumn"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.top: dragArea.bottom
            anchors.leftMargin: Theme.settingsPad
            anchors.rightMargin: Theme.settingsPad
            anchors.bottomMargin: Theme.settingsPad
            anchors.topMargin: Theme.spaceSm
            spacing: Theme.settingsRowGap

            // ── Section heading ──────────────────────────────────────
            // 2026-09-15: the five section headings replace the per-row
            // hairlines that used to separate rows. A hairline above EVERY row
            // read as six equal-weight settings; the headings group them into
            // five named areas (General / Appearance / Files / Updates /
            // Reference) and make the grouping legible at a glance. Same
            // colors as before (textSecondary) — only placement changed.
            Text {
                text: "General"
                height: Theme.settingsSectionLabel
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSubtitle
                font.weight: Theme.fontWeightSemibold
                font.letterSpacing: 0.6
                color: Theme.textSecondary
            }

            // Hotkey row — well click opens the capture dialog.
            Rectangle {
                id: hotkeyRow
                width: parent.width
                height: Theme.settingsRowHotkey
                // Row hover: hoverBg only, never accent (UI-SPEC reserved list).
                color: hotkeyHover.containsMouse ? Theme.hoverBg : "transparent"

                Column {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "Hotkey"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "Click to change"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                    }
                }

                // Hotkey value well — 36px tall, elides on overflow, never
                // reflows the row (UI-SPEC text rule 3).
                Rectangle {
                    id: hotkeyWell
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(Theme.spaceSm * 15, hotkeyWellText.implicitWidth + Theme.spaceLg) // min ~120 (plan)
                    height: Theme.fieldHeight
                    radius: Theme.fieldRadius
                    color: Theme.surfaceSecondary
                    // 2026-08-15 (UI pass): the well's Tab-focus state draws
                    // an accentLight border — same focus identity as the
                    // launcher's search underline (focus is a legit accent
                    // family member; hover never is).
                    border.color: parent.activeFocus ? Theme.accentLight : Theme.border
                    border.width: 1
                    activeFocusOnTab: true
                    Keys.onReturnPressed: (event) => { root.openHotkeyCapture(); event.accepted = true }
                    Keys.onEnterPressed: (event) => { root.openHotkeyCapture(); event.accepted = true }
                    Keys.onSpacePressed: (event) => { root.openHotkeyCapture(); event.accepted = true }
                    Text {
                        id: hotkeyWellText
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spaceSm
                        anchors.rightMargin: Theme.spaceSm
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: root.currentHotkey
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeKeycap
                        font.weight: Theme.fontWeightSemibold
                        color: Theme.textPrimary
                    }
                }

                // Whole row clickable (UI-SPEC Interaction: "Click well (or row)").
                MouseArea {
                    id: hotkeyHover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.openHotkeyCapture()
                }
            }

            // ── Section: Appearance ── (see the General heading for the label style)
            Item { width: 1; height: Theme.settingsSectionGap - Theme.settingsRowGap * 2 }
            Text {
                text: "Appearance"
                height: Theme.settingsSectionLabel
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSubtitle
                font.weight: Theme.fontWeightSemibold
                font.letterSpacing: 0.6
                color: Theme.textSecondary
            }

            // Accent block — header line + swatch strip. The "Custom…" action
            // now sits on the HEADER line at the row's right edge instead of
            // butting against the end of the swatch strip, where it used to
            // read as a tenth swatch. This is the same header+action pattern
            // the Files section uses for "Add folder…".
            Rectangle {
                id: accentRow
                width: parent.width
                height: Theme.settingsRowAccent
                color: "transparent"

                Item {
                    id: accentHeader
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.settingsAccentHeader

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Accent color"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textPrimary
                    }

                    // "Custom…" — pinned right, matching "Add folder…".
                    Item {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: customText.implicitWidth
                        height: Theme.swatchRingSize
                        activeFocusOnTab: true
                        Keys.onReturnPressed: (event) => { root.openColorDialog(); event.accepted = true }
                        Keys.onEnterPressed: (event) => { root.openColorDialog(); event.accepted = true }
                        Keys.onSpacePressed: (event) => { root.openColorDialog(); event.accepted = true }
                        Text {
                            id: customText
                            anchors.centerIn: parent
                            text: "Custom…"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightRegular
                            // Hover: textSecondary -> textPrimary; never accent.
                            color: customHover.containsMouse || parent.activeFocus ? Theme.textPrimary : Theme.textSecondary
                        }
                        MouseArea {
                            id: customHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.openColorDialog()
                        }
                    }
                }

                // Swatch strip — 9 tokens from Theme.accentSwatches (D-05/D-08),
                // on its own line under the header.
                Row {
                    id: swatchStrip
                    anchors.top: accentHeader.bottom
                    anchors.topMargin: Theme.settingsAccentGap
                    anchors.left: parent.left
                    spacing: Theme.swatchGap
                    activeFocusOnTab: true
                    Keys.onLeftPressed: (event) => { root.moveSwatch(-1); event.accepted = true }
                    Keys.onRightPressed: (event) => { root.moveSwatch(+1); event.accepted = true }
                    Keys.onReturnPressed: (event) => { root.commitSwatch(); event.accepted = true }
                    Keys.onEnterPressed: (event) => { root.commitSwatch(); event.accepted = true }
                    Keys.onSpacePressed: (event) => { root.commitSwatch(); event.accepted = true }
                    onActiveFocusChanged: if (!activeFocus) keyboardSwatch = -1
                    Repeater {
                        model: Theme.accentSwatches.length
                        Item {
                            width: Theme.swatchRingSize
                            height: Theme.swatchRingSize
                            // Selection ring — 2px accentLight band (UI-SPEC
                            // accent reserved-for list, selection family).
                            // Snaps, never animates (Animation contract).
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.swatchRadius + Theme.ringWidth
                                color: "transparent"
                                border.color: Theme.accentLight
                                border.width: Theme.ringWidth
                                visible: root.ringIndex === index
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                width: Theme.swatchSize
                                height: Theme.swatchSize
                                radius: Theme.swatchRadius
                                color: Theme.accentSwatches[index]
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.applySwatch(index)
                                }
                            }
                        }
                    }
                }
            }

            // ── Autostart row — toggle ──
            Rectangle {
                id: autostartRow
                width: parent.width
                height: Theme.settingsRowAutostart
                color: "transparent"

                Column {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "Start with Windows"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "Launches quietly in the tray when you sign in"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                    }
                }

                // Toggle — track fills accent when on (D-06 live state), knob
                // slides 120ms (Theme.animFade, UI-SPEC Animation contract).
                Rectangle {
                    id: toggleTrack
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.toggleWidth
                    height: Theme.toggleHeight
                    radius: Theme.toggleTrackRadius
                    color: root.autostartEnabled ? Theme.toggleTrackOn : Theme.toggleTrackOff
                    activeFocusOnTab: true
                    Keys.onReturnPressed: (event) => { root.toggleAutostart(); event.accepted = true }
                    Keys.onEnterPressed: (event) => { root.toggleAutostart(); event.accepted = true }
                    Keys.onSpacePressed: (event) => { root.toggleAutostart(); event.accepted = true }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.toggleAutostart()
                    }
                    Rectangle {
                        id: toggleKnob
                        width: Theme.knobSize
                        height: Theme.knobSize
                        radius: Theme.knobRadius
                        color: Theme.knobColor
                        y: (parent.height - height) / 2
                        // 2px inset — knobSize is track − 2x2 (UI-SPEC).
                        x: root.autostartEnabled ? parent.width - width - 2 : 2
                        Behavior on x { NumberAnimation { duration: Theme.animFade; easing: Easing.Linear } }
                    }
                }
            }

            // ── Section: Files ──
            Item { width: 1; height: Theme.settingsSectionGap - Theme.settingsRowGap * 2 }
            Text {
                text: "Files"
                height: Theme.settingsSectionLabel
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSubtitle
                font.weight: Theme.fontWeightSemibold
                font.letterSpacing: 0.6
                color: Theme.textSecondary
            }

            // Scan locations block — roots list with add/remove (native
            // picker), interval selector, and Scan now. The action sits on the
            // header line at the right edge; text always LEFT (add/remove, the
            // interval selector, Scan now). All values flow through the
            // injected settingsController; the surface never parses the INI.
            Rectangle {
                id: scanRow
                objectName: "settingsScanBlock"
                width: parent.width
                // base + the folders area. Floored at the 72px the base window
                // already carries, so an empty or short list leaves the block
                // (and the 813px window) exactly as it was — only a list that
                // outgrows that budget grows the window (root.foldersExtra).
                height: Theme.settingsRowScanBase
                        + Math.max(foldersArea.height, Theme.settingsRowScanFolders)
                color: "transparent"

                // Section header on TOP (Accent-color pattern) — never
                // vertically centered beside the controls (floating-label
                // inconsistency). 32px: title + subtitle with the Add action
                // on the right (always available).
                Item {
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spaceSm
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.settingsSectionHeader

                    Column {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            text: "Scan locations"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeTitle
                            font.weight: Theme.fontWeightRegular
                            color: Theme.textPrimary
                        }
                        Text {
                            text: "Folders wisp searches for files and folders"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightRegular
                            color: Theme.textSecondary
                        }
                    }

                    // "Add folder" — always available, quiet-stepper family
                    // (secondary well) beside the header, reads as the
                    // section's primary add.
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: addLabel.implicitWidth + Theme.spaceLg * 2
                        height: Theme.settingsRowScanItem
                        radius: Theme.fieldRadius
                        color: addFolderBtn.containsMouse ? Theme.accentDark : Theme.accent
                        Text {
                            id: addLabel
                            anchors.centerIn: parent
                            text: "Add folder…"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightSemibold
                            color: Theme.onAccentText
                        }
                        MouseArea {
                            id: addFolderBtn
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (settingsController)
                                    settingsController.addScanRoot()
                            }
                        }
                    }
                }

                // Controls — full width, stacked under the header with a 10px
                // breathing gap so the folders never sit mushed against the
                // subtitle (base 110 = header 32 + gap 10 + 6 + interval 28 +
                // 6 + action 28, plus the measured folders area below).
                Column {
                    anchors.top: parent.top
                    anchors.topMargin: Theme.settingsSectionHeader + Theme.settingsScanGap
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    spacing: Theme.settingsScanRowGap

                    // Folders area (2026-10-08) — one slot in this Column,
                    // whichever of the two it currently holds:
                    //   - "No folders yet" when the list is empty, or
                    //   - wrapping removable chips, one per scanned folder.
                    //
                    // It was a ScrollView capped at two 36px rows, so a third
                    // folder was only reachable by scrolling inside the list.
                    // Flow wraps instead: every folder is on screen at once, the
                    // block grows with the chip rows, and the window follows
                    // (root.foldersExtra). Nothing here scrolls.
                    //
                    // One Item wrapping both states, not two Column children:
                    // a Column still lays out an invisible child, so a separate
                    // placeholder Text would leave a phantom 28px row (plus its
                    // 6px gap) behind the chips. Its measured height is what
                    // drives the block and window heights, so it must be exact.
                    Item {
                        id: foldersArea
                        // objectName mirrors the id so tests can locate it
                        // (findChild resolves objectName, not a QML id) —
                        // settingsFoldersFillTheBlock_20261008 measures it.
                        objectName: "settingsFoldersArea"
                        width: parent.width
                        // The top pad is INSIDE the slot's height, so it pushes
                        // the chips down off the subtitle AND feeds the block /
                        // window height model below unchanged. Both states are
                        // padded, so switching between empty and populated
                        // doesn't jump.
                        height: Theme.settingsChipPadTop
                                + Math.max(chipsFlow.implicitHeight,
                                           foldersEmpty.visible ? foldersEmpty.height : 0)

                        Text {
                            id: foldersEmpty
                            anchors.top: parent.top
                            anchors.topMargin: Theme.settingsChipPadTop
                            width: parent.width
                            height: Theme.settingsRowScanItem
                            verticalAlignment: Text.AlignVCenter
                            text: "No folders yet"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightRegular
                            color: Theme.textSecondary
                            visible: chipsFlow.count === 0
                        }

                        // Flow gives implicitHeight for free — a Positioner
                        // reports the bounding box of its laid-out children, and
                        // wrapped rows extend that box downward. The block's
                        // height binding above depends on it being exact, which
                        // settingsFoldersFillTheBlock_20261008 asserts.
                        Flow {
                            id: chipsFlow
                            objectName: "settingsFolderChips"
                            anchors.top: parent.top
                            anchors.topMargin: Theme.settingsChipPadTop
                            width: parent.width
                            spacing: Theme.settingsChipGap
                            // Same idiom as the rest of the surface: ask the
                            // controller's list, not Positioner.count, which is
                            // not in scope here (it warned "count is not
                            // defined" and left the Flow permanently visible).
                            visible: settingsController && settingsController.scanRoots.length > 0

                            Repeater {
                                model: settingsController ? settingsController.scanRoots : []
                                delegate: Rectangle {
                                    id: chip
                                    objectName: "settingsFolderChip"
                                    height: Theme.settingsChipHeight
                                    // Label elides past settingsChipMaxTextW so one
                                    // long folder name can't eat a whole row; the
                                    // full path is one hover away (ToolTip below).
                                    width: Theme.settingsChipPadH * 2 + chipLabel.width
                                           + Theme.spaceSm + chipClose.implicitWidth
                                    radius: Theme.fieldRadius
                                    color: chipHover.containsMouse ? Theme.hoverBg : Theme.surfaceSecondary
                                    border.width: 1
                                    border.color: Theme.border

                                    Text {
                                        id: chipLabel
                                        anchors.left: parent.left
                                        anchors.leftMargin: Theme.settingsChipPadH
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.min(implicitWidth, Theme.settingsChipMaxTextW)
                                        elide: Text.ElideMiddle
                                        // Basename, not the full path — the chip
                                        // stays compact and readable; the tooltip
                                        // carries the path for disambiguation.
                                        text: chipFolderName(modelData)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeSubtitle
                                        font.weight: Theme.fontWeightRegular
                                        color: Theme.textSecondary
                                    }

                                    // The × is the ONLY destructive target. Dropping a
                                    // scanned folder is not something a stray click
                                    // on the label should do, so the hit areas are
                                    // split: hover-only on the label, click on the ×.
                                    MouseArea {
                                        id: chipHover
                                        anchors.left: parent.left
                                        anchors.right: chipClose.left
                                        anchors.rightMargin: Theme.spaceSm
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        hoverEnabled: true
                                    }

                                    Text {
                                        id: chipClose
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.settingsChipPadH
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "×"   // U+00D7 — present in every UI font
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeSubtitle
                                        font.weight: Theme.fontWeightRegular
                                        color: chipCloseHover.containsMouse ? Theme.dangerText : Theme.textSecondary
                                    }

                                    MouseArea {
                                        id: chipCloseHover
                                        anchors.fill: chipClose
                                        hoverEnabled: true
                                        onClicked: {
                                            if (settingsController)
                                                settingsController.removeScanRoot(index)
                                        }
                                    }

                                    // Hover the label for the full path — two folders
                                    // can share a basename ("Netch"), and the chip
                                    // alone can't say which is which. No anchors:
                                    // ToolTip is a Popup, not an Item, so it places
                                    // itself over its parent item.
                                    ToolTip {
                                        visible: chipHover.containsMouse
                                        delay: 400
                                        text: modelData
                                    }
                                }
                            }
                        }
                    }

                    // Interval row — label LEFT, ± steppers RIGHT; the clamp
                    // (1..1440) lives in SettingsStore (OQ4), never in the UI.
                    Item {
                        width: parent.width
                        height: Theme.settingsRowScanItem
                        Row {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spaceSm
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Scan every"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightRegular
                                color: Theme.textSecondary
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: settingsController ? settingsController.scanIntervalMinutes + " min" : "10 min"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightSemibold
                                color: Theme.textPrimary
                            }
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spaceSm
                                                // 2026-08-15 (UI pass): −/+ as stepper chips —
                        // 24px wells (same radius family as the field well),
                        // hoverBg fill + textPrimary on hover. Before: bare
                        // floating glyphs, the weakest element of the row.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.stepperSize
                            height: Theme.stepperSize
                            radius: Theme.fieldRadius
                            color: minusHover.containsMouse ? Theme.hoverBg : Theme.surfaceSecondary
                            border.color: Theme.border
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: "−"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightRegular
                                color: minusHover.containsMouse ? Theme.textPrimary : Theme.textSecondary
                            }
                            MouseArea {
                                id: minusHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (settingsController)
                                        settingsController.setScanInterval(settingsController.scanIntervalMinutes - 1)
                                }
                            }
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.stepperSize
                            height: Theme.stepperSize
                            radius: Theme.fieldRadius
                            color: plusHover.containsMouse ? Theme.hoverBg : Theme.surfaceSecondary
                            border.color: Theme.border
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightRegular
                                color: plusHover.containsMouse ? Theme.textPrimary : Theme.textSecondary
                            }
                            MouseArea {
                                id: plusHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (settingsController)
                                        settingsController.setScanInterval(settingsController.scanIntervalMinutes + 1)
                                }
                            }
                        }
                        }
                    }

                    // Action row — last-scan summary LEFT, "Scan now" accent
                    // button RIGHT (all section buttons live on the right).
                    Item {
                        width: parent.width
                        height: Theme.settingsRowScanItem
                        Text {
                            anchors.left: parent.left
                            anchors.right: scanNowBtn.left
                            anchors.rightMargin: Theme.spaceSm
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideMiddle
                            text: settingsController && settingsController.lastScanSummary !== ""
                                  ? settingsController.lastScanSummary : "Not scanned yet"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightRegular
                            color: Theme.textSecondary
                        }
                        Rectangle {
                            id: scanNowBtn
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 84
                            height: Theme.settingsRowScanItem
                            radius: Theme.fieldRadius
                            // 2026-08-15 (UI pass): hover → accentDark (the
                            // derived shade; white label keeps ≥4.5:1 per the
                            // D-15 contrast guard) — the primary action now
                            // reads as pressable, not a static badge.
                            color: scanNowHover.containsMouse ? Theme.accentDark : Theme.accent
                            Text {
                                anchors.centerIn: parent
                                text: "Scan now"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightSemibold
                                color: Theme.onAccentText
                            }
                            MouseArea {
                                id: scanNowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (settingsController)
                                        settingsController.scanNow()
                                }
                            }
                        }
                    }
                }

                    // ── Scan progress bar (2026-08-15) — indeterminate, shown
                    // only while a scan is in flight (settingsController.scanning).
                    // A moving appOutline chunk sweeps the surfaceSecondary
                    // track; the honest "no fake %" choice for a recursive walk
                    // where the total dir count isn't known upfront.
                    //
                    // It never participates in the centered layout, so the
                    // settingsRowScan budget stays put — but it sits in the
                    // inter-section gap BELOW the block, not inside it.
                    // 2026-10-08: the budget (32+10+72+6+28+6+28 = 182) ends
                    // flush with the action row's bottom edge, so any
                    // bottomMargin inside the block lands the bar on top of the
                    // "Last scan …" summary and the "Scan now" button — reported
                    // as an overlap. The 8px settingsRowGap below the block has
                    // room for the 4px track; the negative margin centres it in
                    // that gap, so nothing has to move and no token changes.
                    Item {
                        id: scanBar
                        objectName: "settingsScanBar"
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spaceMd
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: -(Theme.settingsRowGap - Theme.scanBarHeight / 2)
                        width: 260
                        height: Theme.scanBarHeight
                        visible: settingsController && settingsController.scanning
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.surfaceSecondary
                            radius: Theme.scanBarRadius
                        }
                        Rectangle {
                            id: scanChunk
                            width: scanBar.width / 3
                            height: scanBar.height
                            color: Theme.appOutline
                            radius: Theme.scanBarRadius
                            // `value` is undefined until the animation first runs,
                            // which warned "Unable to assign [undefined] to x" on
                            // every settings open. Rest off-screen left, which is
                            // where the animation starts anyway.
                            x: scanChunkAnim.value !== undefined ? scanChunkAnim.value
                                                                  : -scanBar.width
                            SequentialAnimation on x {
                                id: scanChunkAnim
                                running: scanChunk.visible
                                loops: Animation.Infinite
                                PropertyAnimation {
                                    from: -scanBar.width
                                    to: scanBar.width
                                    duration: 900
                                    easing.type: Easing.InOutCubic
                                }
                            }
                        }
                    }
            }

            // ── Updates section (132px, Phase 8 UI-SPEC S1 + header-on-top
            // polish) — auto-install toggle, manual Check button, inline
            // status (D-03/D-10). Text always sits LEFT, every button RIGHT
            // (toggle, Check for updates, Download now). All values flow
            // through the injected settingsController; failures are text
            // here, never popups.
            // ── Section: Updates ──
            Item { width: 1; height: Theme.settingsSectionGap - Theme.settingsRowGap * 2 }
            Text {
                text: "Updates"
                height: Theme.settingsSectionLabel
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSubtitle
                font.weight: Theme.fontWeightSemibold
                font.letterSpacing: 0.6
                color: Theme.textSecondary
            }

            // Updates block — auto-install toggle, manual Check button, inline
            // status (D-03/D-10). Text always sits LEFT, every button RIGHT
            // (toggle, Check for updates, Download now). All values flow
            // through the injected settingsController; failures are text
            // here, never popups. The "Updates / Keep wisp current" header
            // this block used to carry is now the section heading above it, so
            // the subtitle moved onto the toggle row.
            Rectangle {
                id: updatesRow
                width: parent.width
                height: Theme.settingsRowUpdates
                color: "transparent"

                // Controls — full width. The block no longer reserves room for its own
                // header (that is the section heading now), so the toggle row
                // starts at the top edge: toggle row(48) + gap(10) + status
                // row(34) = 92, the settingsRowUpdates budget. The gap is
                // larger than the in-section row gap because the status line
                // is the result of the toggle, not another peer row — it needs
                // to read as belonging to it, but not be glued to it.
                Column {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    spacing: Theme.settingsUpdatesGap

                    // Auto-install toggle row — track fills accent when on;
                    // sub-line states the zero-interaction contract (D-05).
                    // Height is settingsRowSingle so it matches every other
                    // label+sub+control row in the surface.
                    Row {
                        width: parent.width
                        height: Theme.settingsRowSingle
                        spacing: Theme.spaceSm
                        Column {
                            width: parent.width - Theme.toggleWidth - Theme.spaceSm
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: "Install updates automatically"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeTitle
                                font.weight: Theme.fontWeightSemibold
                                color: Theme.textPrimary
                            }
                            Text {
                                text: "wisp restarts itself to finish updates"
                                visible: settingsController && settingsController.updatesAutoInstall
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightRegular
                                color: Theme.textSecondary
                            }
                        }
                        Rectangle {
                            id: updatesToggleTrack
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.toggleWidth
                            height: Theme.toggleHeight
                            radius: Theme.toggleTrackRadius
                            // PROPERTY binding (no parens) - re-evaluates on
                            // updatesAutoInstallChanged (05.1 lesson).
                            color: settingsController && settingsController.updatesAutoInstall
                                   ? Theme.toggleTrackOn : Theme.toggleTrackOff
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (settingsController)
                                        settingsController.setUpdatesAutoInstall(
                                            !settingsController.updatesAutoInstall)
                                }
                            }
                            Rectangle {
                                width: Theme.knobSize
                                height: Theme.knobSize
                                radius: Theme.knobRadius
                                color: Theme.knobColor
                                y: (parent.height - height) / 2
                                x: settingsController && settingsController.updatesAutoInstall
                                   ? parent.width - width - 2 : 2
                                Behavior on x { NumberAnimation { duration: Theme.animFade; easing: Easing.Linear } }
                            }
                        }
                    }

                    // Check row — status + hint stacked LEFT, Check/Download
                    // buttons RIGHT (all section buttons live on the right).
                    // While an update is pending the status turns bold
                    // textPrimary and the Download now button appears (D-03).
                    Item {
                        width: parent.width
                        height: Theme.settingsRowUpdatesCheck
                        Row {
                            id: checkBtns
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spaceSm
                            Rectangle {
                                visible: settingsController && settingsController.updateAvailable
                                anchors.verticalCenter: parent.verticalCenter
                                width: dlLabel.implicitWidth + 24
                                height: Theme.settingsRowScanItem
                                radius: Theme.fieldRadius
                                color: dlBtn.containsMouse ? Theme.accentDark : Theme.accent
                                Text {
                                    id: dlLabel
                                    anchors.centerIn: parent
                                    text: "Download now"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSubtitle
                                    font.weight: Theme.fontWeightSemibold
                                    color: Theme.onAccentText
                                }
                                MouseArea {
                                    id: dlBtn
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (settingsController)
                                            settingsController.downloadPendingUpdate()
                                    }
                                }
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: checkLabel.implicitWidth + 24
                                height: Theme.settingsRowScanItem
                                radius: Theme.fieldRadius
                                color: checkBtn.containsMouse ? Theme.accentDark : Theme.accent
                                Text {
                                    id: checkLabel
                                    anchors.centerIn: parent
                                    text: "Check for updates"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSubtitle
                                    font.weight: Theme.fontWeightSemibold
                                    color: Theme.onAccentText
                                }
                                MouseArea {
                                    id: checkBtn
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (settingsController)
                                            settingsController.checkForUpdatesNow()
                                    }
                                }
                            }
                        }
                        // Status line — plain outcome, % included while
                        // downloading; hint below answers "what happens
                        // next" and hides itself when empty (Checking).
                        Column {
                            anchors.left: parent.left
                            anchors.right: checkBtns.left
                            anchors.rightMargin: Theme.spaceSm
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: parent.width
                                height: 20
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                // PROPERTY binding (no parens) — refreshes on
                                // updateStatusChanged from any engine transition.
                                text: settingsController ? settingsController.updateStatus
                                                         : "Not checked yet"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: settingsController && settingsController.updateAvailable
                                             ? Theme.fontWeightSemibold : Theme.fontWeightRegular
                                color: settingsController && settingsController.updateAvailable
                                       ? Theme.textPrimary : Theme.textSecondary
                            }
                            Text {
                                width: parent.width
                                height: visible ? 16 : 0
                                visible: settingsController && settingsController.updateHint !== ""
                                elide: Text.ElideRight
                                text: settingsController ? settingsController.updateHint : ""
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightRegular
                                color: Theme.textSecondary
                            }
                        }
                    }

                    // Download bar — determinate accent fill while a download
                    // is in flight (scan-bar tokens; UX pass moved progress
                    // INTO Settings; the floating window is gone). Collapses
                    // to 0 when idle so it never reserves layout space.
                    Item {
                        width: parent.width
                        height: visible ? Theme.scanBarHeight + 2 : 0
                        visible: settingsController && settingsController.updateDownloading

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.scanBarRadius
                            color: Theme.surfaceSecondary
                        }
                        Rectangle {
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            width: (settingsController ? settingsController.downloadRatio : 0)
                                   * parent.width
                            radius: Theme.scanBarRadius
                            color: Theme.accent
                        }
                    }
                }
            }

            // ── Section: Reference ──
            Item { width: 1; height: Theme.settingsSectionGap - Theme.settingsRowGap * 2 }
            Text {
                text: "Reference"
                height: Theme.settingsSectionLabel
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSubtitle
                font.weight: Theme.fontWeightSemibold
                font.letterSpacing: 0.6
                color: Theme.textSecondary
            }

            // Shortcuts row — opens the themed ShortcutsWindow. Left:
            // label+sub; right: accent button (scan-now/check-button family).
            Rectangle {
                id: shortcutsRow
                width: parent.width
                height: Theme.settingsRowShortcuts
                color: "transparent"

                Column {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spaceXs
                    Text {
                        text: "Shortcuts"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "Every key wisp understands"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                    }
                }

                Rectangle {
                    id: shortcutsBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: shortcutsLabel.implicitWidth + Theme.spaceLg * 2
                    height: Theme.settingsRowScanItem
                    radius: Theme.fieldRadius
                    color: shortcutsHover.containsMouse ? Theme.accentDark : Theme.accent
                    Text {
                        id: shortcutsLabel
                        anchors.centerIn: parent
                        text: "Show shortcuts"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightSemibold
                        color: Theme.onAccentText
                    }
                    MouseArea {
                        id: shortcutsHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.openShortcuts()
                    }
                }
            }
        }
    }
}
