import QtQuick
import QtQuick.Window
import wisp

// D-07 custom color dialog (06-UI-SPEC Geometry & Window Contract) — the
// staged surface (D-06): edits stage internally; OK commits via
// settingsController.commitCustomColor(hex) (06-03 → SettingsStore.setAccent,
// persist + live apply); Cancel/Esc discards. The hex string is GENERATED
// from the staged HSV state, never freeform text (T-06-01).
//
// Injected host: `settingsController` (beginCreate/setProperty — capture-
// dialog precedent). No animation, instant show + focus (Animation contract).
Window {
    id: root
    title: "Custom color"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.ApplicationModal
    width: Theme.colorDialogWindowWidth      // 280
    height: Theme.colorDialogWindowHeight    // 320
    color: "transparent"
    modality: Qt.ApplicationModal
    visible: false   // the controller shows it (06-03)

    // NOT readonly (2026-08-12): setProperty on a readonly QML property is a
    // silent no-op — the host injection never landed and the commit was dead.
    property var settingsController: null

    // Staged HSV state — seeded from Theme.accent on every open (D-16 silent
    // fallback: Theme.accent is always valid). Nothing persists until OK.
    property real stageHue: 0.58
    property real stageSat: 0.83
    property real stageVal: 0.83

    function hexByte(v) {
        var n = Math.round(v * 255).toString(16)
        return n.length < 2 ? "0" + n : n
    }
    // T-06-01: hex is generated from the staged HSV state — no freeform input.
    function hsvToHex(h, s, v) {
        var i = Math.floor(h * 6)
        var f = h * 6 - i
        var p = v * (1 - s)
        var q = v * (1 - f * s)
        var t = v * (1 - (1 - f) * s)
        var r = 0, g = 0, b = 0
        switch (i % 6) {
        case 0: r = v; g = t; b = p; break
        case 1: r = q; g = v; b = p; break
        case 2: r = p; g = v; b = t; break
        case 3: r = p; g = q; b = v; break
        case 4: r = t; g = p; b = v; break
        default: r = v; g = p; b = q; break
        }
        return "#" + hexByte(r) + hexByte(g) + hexByte(b)
    }
    property string hexReadout: hsvToHex(stageHue, stageSat, stageVal)

    function updateSatVal(x, y) {
        stageSat = Math.max(0, Math.min(1, x / Theme.svSize))
        stageVal = Math.max(0, Math.min(1, 1 - y / Theme.svSize))
    }
    function updateHue(y) {
        stageHue = Math.max(0, Math.min(1, y / Theme.svSize))
    }
    function commit() {
        if (settingsController)
            settingsController.commitCustomColor(root.hexReadout)
        root.hide()
        root.close()
    }
    function cancel() {
        root.hide()
        root.close()
    }

    // Centered on primary screen (capture-dialog geometry logic).
    Component.onCompleted: {
        x = Screen.availableX + Math.round((Screen.availableWidth - width) / 2)
        y = Screen.availableY + Math.round((Screen.availableHeight - height) / 2)
    }

    // Staging contract (D-06): every open re-seeds from the current accent;
    // edits only touch the staged state; Cancel/Esc discards.
    onVisibleChanged: {
        if (visible) {
            stageHue = Theme.accent.hsvHue
            stageSat = Theme.accent.hsvSaturation
            stageVal = Theme.accent.hsvValue
            panel.forceActiveFocus()
        }
    }

    // Static pre-rendered shadow — same shell as the capture dialog.
    Image {
        anchors.fill: parent
        source: "assets/shadow.png"
        opacity: Theme.shadowOpacity
    }

    // The surface (248x288 = 280/320 − 2x16 shadow margin).
    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Theme.colorDialogSurfaceWidth
        height: Theme.colorDialogSurfaceHeight
        radius: Theme.radiusSurface
        color: Theme.surface
        border.color: Theme.border
        border.width: 1
        clip: true
        focus: true

        // Window-level keys: Esc discards, Enter confirms (UI-SPEC Focus).
        Keys.onEscapePressed: (event) => { root.cancel(); event.accepted = true }
        Keys.onReturnPressed: (event) => { root.commit(); event.accepted = true }
        Keys.onEnterPressed: (event) => { root.commit(); event.accepted = true }

        Column {
            anchors.fill: parent
            anchors.margins: Theme.spaceLg   // 16
            spacing: Theme.spaceLg

            Text {
                text: "Custom color"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightSemibold
                color: Theme.textPrimary
            }

            // Color area: SV square + vertical hue bar.
            Row {
                width: parent.width
                spacing: Theme.spaceMd

                // SV square — two-stop hue gradient (the CURRENT hue) plus a
                // vertical transparent→black overlay (UI-SPEC Geometry).
                Rectangle {
                    id: svSquare
                    width: Theme.svSize
                    height: Theme.svSize
                    radius: Theme.fieldRadius
                    clip: true
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "white" }
                        GradientStop { position: 1.0; color: Qt.hsla(root.stageHue, 1.0, 0.5, 1.0) }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 1.0; color: "black" }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.CrossCursor
                        onPressed: (m) => root.updateSatVal(m.x, m.y)
                        onPositionChanged: (m) => { if (m.buttons & Qt.LeftButton) root.updateSatVal(m.x, m.y) }
                    }
                }

                // Vertical hue bar — rainbow gradient, drag changes hue.
                Rectangle {
                    id: hueBar
                    width: Theme.hueBarWidth
                    height: Theme.svSize
                    radius: Theme.hueBarRadius
                    clip: true
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "red" }
                        GradientStop { position: 0.17; color: "yellow" }
                        GradientStop { position: 0.33; color: "green" }
                        GradientStop { position: 0.5; color: "cyan" }
                        GradientStop { position: 0.67; color: "blue" }
                        GradientStop { position: 0.83; color: "magenta" }
                        GradientStop { position: 1.0; color: "red" }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.CrossCursor
                        onPressed: (m) => root.updateHue(m.y)
                        onPositionChanged: (m) => { if (m.buttons & Qt.LeftButton) root.updateHue(m.y) }
                    }
                }
            }

            // Bottom bar: live preview + hex readout (left), OK/Cancel (right).
            // Buttons are the EXACT capture-dialog geometry: 90x32, radius 6.
            Item {
                width: parent.width
                height: Theme.space2xl   // 32 — matches the button height

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spaceSm
                    Rectangle {
                        width: Theme.space2xl
                        height: Theme.space2xl
                        radius: Theme.fieldRadius
                        color: root.hexReadout
                        border.color: Theme.border
                        border.width: 1
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.hexReadout
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spaceMd

                    // Cancel — surfaceSecondary (never accent).
                    Rectangle {
                        width: 90
                        height: Theme.space2xl
                        radius: Theme.fieldRadius
                        color: Theme.surfaceSecondary
                        border.color: Theme.border
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "Cancel"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightSemibold
                            color: Theme.textPrimary
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.cancel()
                        }
                    }

                    // OK — accent fill + white text (declared primary-button
                    // exception, capture-dialog precedent; UI-SPEC Color).
                    Rectangle {
                        width: 90
                        height: Theme.space2xl
                        radius: Theme.fieldRadius
                        color: Theme.accent
                        border.color: Theme.accent
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "OK"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightSemibold
                            color: Theme.onAccentText
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.commit()
                        }
                    }
                }
            }
        }
    }
}
