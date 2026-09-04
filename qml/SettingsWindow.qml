import QtQuick
import QtQuick.Controls
import QtQuick.Window
import wisp

// SYS-03 / D-01/D-02: the settings surface — a dedicated dark tool window,
// 480x812 (token-only, D-08; grew 736→812 with the Phase 12 Shortcuts row).
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
    width: Theme.settingsWindowWidth
    height: Theme.settingsWindowHeight
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
    Component.onCompleted: {
        x = Screen.availableX + Math.round((Screen.availableWidth - width) / 2)
        y = Screen.availableY + Math.round((Screen.availableHeight - height) / 2)
    }

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

    // Static pre-rendered shadow — same shell as MainWindow (assets/shadow.png,
    // 16px margin, opacity 0.45).
    Image {
        anchors.fill: parent
        source: "assets/shadow.png"
        opacity: Theme.shadowOpacity
    }

    // The surface (448x780 + 2x16 shadow margin inside the 480x812 window).
    Rectangle {
        id: surface
        anchors.centerIn: parent
        width: Theme.settingsSurfaceWidth
        height: Theme.settingsSurfaceHeight
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

        // Content column — vertical budget (growth precedent: 07-06 +scan,
        // Phase 8 +updates, Phase 12 +show-shortcuts + header-on-top polish):
        // 8 top + 6 rows 64/88/64/200/160/64 (640) + 5×12 gaps (60) + 24
        // bottom pad = 732 within the 768 available (832 surface − 32 drag
        // header − 32 margins) — no clipping (research OQ1: growth via
        // tokens, not a ScrollView).
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.top: dragArea.bottom
            anchors.leftMargin: Theme.settingsPad
            anchors.rightMargin: Theme.settingsPad
            anchors.bottomMargin: Theme.settingsPad
            anchors.topMargin: Theme.spaceSm
            spacing: Theme.settingsRowGap

            // ── Hotkey row (64px) — well click opens the capture dialog ──
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

            // ── Accent row (88px) — swatch strip + custom entry ──
            Rectangle {
                id: accentRow
                width: parent.width
                height: Theme.settingsRowAccent
                color: "transparent"
                // 1px hairline separator from the row above (UI-SPEC hairlines).
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.separator
                }

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spaceSm

                    Text {
                        text: "Accent color"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textPrimary
                    }

                    Row {
                        spacing: Theme.spaceLg   // 16px gap before "Custom…" (UI-SPEC layout)

                        // Swatch strip — 9 tokens from Theme.accentSwatches
                        // (D-05/D-08), 28px ring footprint each.
                        Row {
                            id: swatchStrip
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
                                    // Selection ring — 2px accentLight band
                                    // (UI-SPEC accent reserved-for list,
                                    // selection family). Snaps, never
                                    // animates (Animation contract).
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

                        // "Custom…" text button (opens the staged dialog, D-07).
                        Item {
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
                                // Hover: textSecondary → textPrimary; never accent.
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
                }
            }

            // ── Autostart row (64px) — toggle ──
            Rectangle {
                id: autostartRow
                width: parent.width
                height: Theme.settingsRowAutostart
                color: "transparent"
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.separator
                }

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

            // ── Scan locations row (158px, 07-05 D-10) — roots list with
            // add/remove (native picker), ± interval selector, Scan now, and
            // the last-scan summary. All values flow through the injected
            // settingsController; the surface never parses the INI.
            Rectangle {
                id: scanRow
                width: parent.width
                height: Theme.settingsRowScan
                color: "transparent"
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.separator
                }

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

                // Controls — full width, stacked under the header
                // (roots 56 + 4 + interval 28 + 4 + action 28 = 120 within
                // the 200-row minus the 32px header).
                Column {
                    anchors.top: parent.top
                    anchors.topMargin: Theme.settingsSectionHeader + Theme.spaceSm
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spaceSm
                    spacing: Theme.spaceXs

                    // Empty placeholder — shown only when no roots yet.
                    Text {
                        width: parent.width
                        height: Theme.settingsRowScanItem
                        verticalAlignment: Text.AlignVCenter
                        text: "No folders yet"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                        visible: !(settingsController && settingsController.scanRoots.length > 0)
                    }

                    // Root list — height-capped (research Pitfall 3: no
                    // full-height ListView), token-driven height.
                    ScrollView {
                        width: parent.width
                        height: (settingsController && settingsController.scanRoots.length > 0)
                               ? Math.min(settingsController.scanRoots.length, 2) * Theme.settingsRowScanItem
                               : 0
                        clip: true
                        contentWidth: width
                        visible: settingsController && settingsController.scanRoots.length > 0
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        Column {
                            width: parent.width
                            Repeater {
                                model: settingsController ? settingsController.scanRoots : []
                                Rectangle {
                                    width: parent.width
                                    height: Theme.settingsRowScanItem
                                    color: "transparent"
                                    // Hairline separator under every entry but
                                    // the last (consistency with the rest of
                                    // the window).
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        height: 1
                                        color: Theme.separator
                                        visible: index < (settingsController ? settingsController.scanRoots.length : 0) - 1
                                    }
                                    Text {
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 64
                                        elide: Text.ElideMiddle
                                        text: modelData
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeSubtitle
                                        font.weight: Theme.fontWeightRegular
                                        color: Theme.textSecondary
                                    }
                                    // Remove — a real button now (stepper-chip
                                    // family): bordered well, hover fill,
                                    // danger-red text on hover so it reads as
                                    // destructive, not a dim label.
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: removeLabel.implicitWidth + Theme.spaceLg
                                        height: 24
                                        radius: Theme.fieldRadius
                                        color: removeHover.containsMouse ? Theme.hoverBg : "transparent"
                                        border.width: 1
                                        border.color: removeHover.containsMouse ? Theme.dangerBorder : Theme.border
                                        Text {
                                            id: removeLabel
                                            anchors.centerIn: parent
                                            text: "Remove"
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeSubtitle
                                            font.weight: Theme.fontWeightSemibold
                                            color: removeHover.containsMouse ? Theme.dangerText : Theme.textSecondary
                                        }
                                        MouseArea {
                                            id: removeHover
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: {
                                                if (settingsController)
                                                    settingsController.removeScanRoot(index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Interval row — ± buttons; the clamp (1..1440) lives in
                    // SettingsStore (OQ4), never in the UI.
                    Row {
                        width: parent.width
                        height: Theme.settingsRowScanItem
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

                    // Action row — "Scan now" accent button + last-scan summary.
                    Row {
                        width: parent.width
                        height: Theme.settingsRowScanItem
                        spacing: Theme.spaceSm
                        Rectangle {
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
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 84 - Theme.spaceSm
                            elide: Text.ElideMiddle
                            text: settingsController && settingsController.lastScanSummary !== ""
                                  ? settingsController.lastScanSummary : "Not scanned yet"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightRegular
                            color: Theme.textSecondary
                        }
                    }
                }

                    // ── Scan progress bar (2026-08-15) — indeterminate, shown
                    // only while a scan is in flight (settingsController.scanning).
                    // A moving appOutline chunk sweeps the surfaceSecondary
                    // track; the honest "no fake %" choice for a recursive walk
                    // where the total dir count isn't known upfront. Overlays
                    // the section bottom (never participates in the centered
                    // layout, so the fixed 170px budget stays put).
                    Item {
                        id: scanBar
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spaceMd
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Theme.spaceSm
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
                            x: scanChunkAnim.value
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

            // ── Updates section (160px, Phase 8 UI-SPEC S1 + header-on-top
            // polish) — auto-install toggle, manual Check button, inline
            // status (D-03/D-10). All values flow through the injected
            // settingsController; failures are text here, never popups.
            Rectangle {
                id: updatesRow
                width: parent.width
                height: Theme.settingsRowUpdates
                color: "transparent"
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.separator
                }

                // Section header on TOP — matches the scan-locations header
                // (32px: title + subtitle).
                Column {
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spaceSm
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 2
                    Text {
                        text: "Updates"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "Keep wisp current"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                    }
                }

                // Controls — full width, stacked under the header
                // (auto-toggle 28 + 8 + check 28 + 8 + status 20 + 8 + hint
                // 16 = 108 within the 160-row minus the 32px header).
                Column {
                    anchors.top: parent.top
                    anchors.topMargin: Theme.settingsSectionHeader + Theme.spaceSm
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spaceSm
                    spacing: Theme.spaceXs

                    // Auto-install toggle row — track fills accent when on;
                    // sub-line states the zero-interaction contract (D-05).
                    Row {
                        width: parent.width
                        height: Theme.settingsRowScanItem
                        spacing: Theme.spaceSm
                        Column {
                            width: parent.width - Theme.toggleWidth - Theme.spaceSm
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: "Install updates automatically"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
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

                    // Check row — accent button (Scan-now clone, but
                    // content-sized: "Check for updates" outgrows the fixed
                    // 84px Scan-now width) + reactive inline status; while an
                    // update is pending the status turns bold textPrimary and
                    // a Download now button appears (D-03).
                    Row {
                        width: parent.width
                        height: Theme.settingsRowScanItem
                        spacing: Theme.spaceSm
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
                    }

                    // Download bar — determinate accent fill while a download
                    // is in flight (scan-bar tokens; UX pass moved progress
                    // INTO Settings; the floating window is gone).
                    Item {
                        width: parent.width
                        height: Theme.scanBarHeight + 2
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

                    // Status line — plain outcome, % included while downloading.
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

                    // Hint line — the "what happens next" answer for every
                    // state. Hides itself when empty (Checking).
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

            // ── Show shortcuts row (64px, Phase 12) — opens the themed
            // ShortcutsWindow reference. Left: label+sub; right: accent
            // button (scan-now/check-button family).
            Rectangle {
                id: shortcutsRow
                width: parent.width
                height: Theme.settingsRowShortcuts
                color: "transparent"
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.separator
                }

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
