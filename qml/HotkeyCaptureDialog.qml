import QtQuick
import QtQuick.Window
import wisp

// D-02.6: theme-driven hotkey capture (HOTK-01 configurable). The C++ host
// (HotkeyCaptureDialog) is injected as `dialogHost` on open — QML has no
// access to it otherwise. Ok routes through host.submitSequence, which
// validates (F12 / modifier-only rejected) and either emits accepted() or
// asks QML to show the red rejection label.
//
// NOTE: buttons are written out concretely (no Qt Quick Controls, no inline
// `component` — this Qt 6.11 toolchain's qmlcachegen rejects inline
// components with a syntax error).
Window {
    id: root
    title: "wisp — change hotkey"
    flags: Qt.FramelessWindowHint | Qt.Tool
    width: 360
    height: 180
    color: "transparent"
    modality: Qt.ApplicationModal

    // NOT readonly (2026-08-12): setProperty on a readonly QML property is a
    // silent no-op — the host injection never landed and OK was dead.
    property var dialogHost: null
    property string currentSequence: "Alt+Space"

    // Centered on primary screen (mirrors MainWindow geometry logic).
    Component.onCompleted: {
        x = Screen.availableX + Math.round((Screen.availableWidth - width) / 2)
        y = Screen.availableY + Math.round((Screen.availableHeight - height) / 2)
        show()
        keyField.forceActiveFocus()
    }

    // ── Portable capture composition (deterministic, no QML value-type
    //    dependence): splits event.key/event.modifiers into portable tokens
    //    in QKeySequence order (Ctrl, Alt, Shift, Meta + key). ──
    function keyToken(key) {
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return String.fromCharCode(key)
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key)
        if (key >= Qt.Key_F1 && key <= Qt.Key_F24)
            return "F" + (key - Qt.Key_F1 + 1)
        var named = {
            [Qt.Key_Space]: "Space", [Qt.Key_Tab]: "Tab", [Qt.Key_Return]: "Return",
            [Qt.Key_Enter]: "Enter", [Qt.Key_Escape]: "Esc", [Qt.Key_Backspace]: "Backspace",
            [Qt.Key_Delete]: "Del", [Qt.Key_Insert]: "Ins", [Qt.Key_Home]: "Home",
            [Qt.Key_End]: "End", [Qt.Key_PageUp]: "PgUp", [Qt.Key_PageDown]: "PgDown",
            [Qt.Key_Up]: "Up", [Qt.Key_Down]: "Down", [Qt.Key_Left]: "Left",
            [Qt.Key_Right]: "Right"
        }
        return named[key] !== undefined ? named[key] : ""
    }

    function composePortable(key, mods) {
        var tokens = []
        if (mods & Qt.ControlModifier) tokens.push("Ctrl")
        if (mods & Qt.AltModifier) tokens.push("Alt")
        if (mods & Qt.ShiftModifier) tokens.push("Shift")
        if (mods & Qt.MetaModifier) tokens.push("Meta")
        var k = keyToken(key)
        if (k) tokens.push(k)
        return tokens.join("+")
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        radius: Theme.radiusSurface
        color: Theme.surface
        border.color: Theme.border
        border.width: 1

        Column {
            anchors.fill: parent
            anchors.margins: Theme.spaceXl
            spacing: Theme.spaceMd

            Text {
                text: "Change hotkey"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightSemibold
                color: Theme.textPrimary
            }

            // Capture surface: every key press composes the portable sequence.
            Item {
                id: keyField
                width: parent.width
                height: Theme.fieldHeight

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.fieldRadius
                    color: Theme.surfaceSecondary
                    border.color: Theme.border
                    border.width: 1
                }
                Text {
                    anchors.fill: parent
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeQuery
                    font.weight: Theme.fontWeightSemibold
                    color: Theme.accentLight
                    text: capturedSequence ? capturedSequence : "Press keys…"
                }

                Keys.onPressed: (event) => {
                    // Plain Escape cancels (D-02.6); any other key composes.
                    if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
                        event.accepted = true
                        cancelDialog()
                        return
                    }
                    event.accepted = true
                    capturedSequence = composePortable(event.key, event.modifiers)
                }
            }

            Text {
                id: hint
                width: parent.width
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSubtitle
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                text: "Press the new key combination…"

                // Red rejection feedback; invoked by host.submitSequence.
                function showValidationError() {
                    color = Theme.danger
                    text = capturedSequence.indexOf("F12") >= 0
                        ? "F12 is reserved by Windows and cannot be used"
                        : "Modifier keys alone are not a valid combination"
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spaceMd
                height: 32

                // Cancel
                Rectangle {
                    width: 90
                    height: 32
                    radius: 6
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
                        onClicked: cancelDialog()
                    }
                }

                // OK
                Rectangle {
                    width: 90
                    height: 32
                    radius: 6
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
                        onClicked: {
                            if (dialogHost)
                                dialogHost.submitSequence(capturedSequence)
                        }
                    }
                }
            }
        }
    }

    function cancelDialog() {
        root.hide()
        root.close()
        if (dialogHost)
            dialogHost.cancelled()
    }

    property string capturedSequence: ""

    // Keep the capture state tidy across opens.
    onVisibleChanged: {
        if (!visible)
            capturedSequence = ""
    }
}