import QtQuick
import QtQuick.Window
import wisp

// SYS-03 / D-01/D-02: the settings surface — a dedicated dark tool window,
// 480x360 fixed (UI-SPEC Geometry), token-only (D-08). Pure QML view: the
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
    width: Theme.settingsWindowWidth      // 480
    height: Theme.settingsWindowHeight    // 360
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

    // Static pre-rendered shadow — same shell as MainWindow (assets/shadow.png,
    // 16px margin, opacity 0.45).
    Image {
        anchors.fill: parent
        source: "assets/shadow.png"
        opacity: Theme.shadowOpacity
    }

    // The surface (448x328 + 2x16 shadow margin inside the 480x360 window).
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

        // Content column — vertical budget 318 <= 328 (UI-SPEC declared):
        // 24 pad + 18 header + 12 + 64 + 12 + 88 + 12 + 64 + 24 pad.
        Column {
            anchors.fill: parent
            anchors.margins: Theme.settingsPad
            spacing: Theme.settingsRowGap

            Text {
                text: "Settings"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightSemibold
                color: Theme.textPrimary
            }

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
                    border.color: Theme.border
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
                    color: Theme.border
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
                    color: Theme.border
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
        }
    }
}
