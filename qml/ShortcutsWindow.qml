import QtQuick
import QtQuick.Controls
import QtQuick.Window
import wisp

// Phase 12: the keyboard-shortcuts reference window — opened from a
// "Show shortcuts" button in SettingsWindow.qml via the injected
// settingsController.openShortcuts(). Same chrome family as SettingsWindow
// / ColorDialog: frameless Tool window, static shadow, theme tokens, drag
// header + close X. Content is a static list of {label, keys[]} rows rendered
// with labelTextPrimary + surfaceSecondary keycap chips (fontSizeKeycap).
//
// Injected host: `settingsController` (beginCreate/setProperty — capture-
// dialog precedent). Controller-owned dismissal semantics (Esc → close).
Window {
    id: root
    title: "wisp — keyboard shortcuts"
    flags: Qt.Tool | Qt.FramelessWindowHint
    width: Theme.shortcutsWindowWidth
    height: Theme.shortcutsWindowHeight
    color: "transparent"
    visible: false   // the controller shows it (SettingsWindow::openShortcuts)

    property var settingsController: null

    // The reference data — single list rendered by every row. Each entry:
    //   label       — the action name (left column)
    //   description — a one-line "what this does" note under the label
    //   keys        — keycap chips (a "+" joiner is drawn between them)
    // Kept here (not C++): the bindings are defined at the point of use in
    // MainWindow.qml / SettingsWindow.qml, so the reference lives beside the
    // controls it documents.
    property var shortcuts: [
        { label: "Open wisp", description: "Show or hide the launcher from anywhere", keys: ["Alt", "Space"] },
        { label: "Run selection", description: "Launch the highlighted result", keys: ["Enter"] },
        { label: "Open containing folder", description: "Reveal a file's folder in Explorer", keys: ["Ctrl", "Enter"] },
        { label: "Run as administrator", description: "Launch the result with elevated rights", keys: ["Ctrl", "Shift", "Enter"] },
        { label: "Quick-select result", description: "Jump straight to result 1 through 9", keys: ["Alt", "1"] },
        { label: "Navigate results", description: "Move the selection through the list", keys: ["\u2191", "\u2193"] },
        { label: "Page through results", description: "Jump by whole screens of results", keys: ["PgUp", "PgDn"] },
        { label: "First / last result", description: "Jump to the top or bottom of the list", keys: ["Home", "End"] },
        { label: "Show hidden entries", description: "Toggle entries you have hidden", keys: ["Ctrl", "H"] },
        { label: "Run a shell command", description: "Type cmd/ then a command and press Enter", keys: ["cmd/", "Enter"] },
        { label: "Close launcher", description: "Dismiss the window without launching", keys: ["Esc"] }
    ]

    function closeWindow() {
        root.hide()
        root.close()
    }

    Component.onCompleted: {
        x = Screen.availableX + Math.round((Screen.availableWidth - width) / 2)
        y = Screen.availableY + Math.round((Screen.availableHeight - height) / 2)
    }

    // Static pre-rendered shadow — same shell as SettingsWindow.
    Image {
        anchors.fill: parent
        source: "assets/shadow.png"
        opacity: Theme.shadowOpacity
    }

    // The surface (528x548 + 2x16 shadow margin inside the 560x580 window).
    Rectangle {
        id: surface
        anchors.centerIn: parent
        width: Theme.shortcutsSurfaceWidth
        height: Theme.shortcutsSurfaceHeight
        radius: Theme.radiusSurface
        color: Theme.surface
        border.color: Theme.border
        border.width: 1
        clip: true
        focus: true
        Keys.onEscapePressed: (event) => { root.closeWindow(); event.accepted = true }

        // Drag header — entire top bar drags the frameless window.
        MouseArea {
            id: dragArea
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: closeBtn.left
            anchors.rightMargin: Theme.spaceSm
            height: 32
            onPressed: (mouse) => root.startSystemMove()
            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.settingsPad
                anchors.top: parent.top
                anchors.topMargin: 10
                text: "Keyboard shortcuts"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightSemibold
                color: Theme.textPrimary
            }
        }

        // X close button — top-right of the surface.
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
                onClicked: root.closeWindow()
            }
        }

        // Shortcut list — a height-capped ScrollView (scan-roots precedent);
        // rows are 44px with 4px gaps: title + description (left), keycap
        // chips (right, vertically centered).
        ScrollView {
            anchors.top: dragArea.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.settingsPad
            anchors.rightMargin: Theme.settingsPad
            anchors.bottomMargin: Theme.settingsPad
            anchors.topMargin: Theme.spaceSm
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            contentWidth: width

            Column {
                width: parent.width
                spacing: Theme.shortcutRowGap

                Repeater {
                    model: root.shortcuts

                    // One shortcut: label + description (left), keycaps right.
                    Rectangle {
                        width: parent.width
                        height: Theme.shortcutRowHeight
                        color: "transparent"

                        // Hairline separator under every row but the last
                        // (accent-derived Theme.separator, settings-row family).
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 1
                            color: Theme.separator
                            visible: index < root.shortcuts.length - 1
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.right: keycapsCol.left
                            anchors.rightMargin: Theme.spaceLg
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Text {
                                text: modelData.label
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightSemibold
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                                width: parent.width
                            }
                            Text {
                                text: modelData.description
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeKeycap
                                font.weight: Theme.fontWeightRegular
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }

                        // Keycap chips — right-aligned. A "+" joiner chip sits
                        // between multi-key chords (render joiners inline).
                        Row {
                            id: keycapsCol
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spaceXs
                            Repeater {
                                // Interleave keys and "+" joiners.
                                model: {
                                    var items = []
                                    for (var i = 0; i < modelData.keys.length; ++i) {
                                        if (i > 0)
                                            items.push({ isJoin: true })
                                        items.push({ isJoin: false, text: modelData.keys[i] })
                                    }
                                    return items
                                }
                                // Joiner = thin "+" text; key = chip. Both
                                // get a fixed 28px strip (chip height) so the
                                // holding Row sizes from content — NOT
                                // parent.height, which looped to 0 (chips
                                // vanished, "only a title" bug).
                                Item {
                                    width: modelData.isJoin
                                           ? joinLabel.implicitWidth
                                           : chipLabel.implicitWidth + Theme.spaceLg
                                    height: Theme.spaceSm * 2 + Theme.fontSizeKeycap
                                    Text {
                                        id: joinLabel
                                        visible: modelData.isJoin
                                        anchors.centerIn: parent
                                        text: "+"
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeKeycap
                                        font.weight: Theme.fontWeightRegular
                                        color: Theme.textSecondary
                                    }
                                    Rectangle {
                                        visible: !modelData.isJoin
                                        anchors.centerIn: parent
                                        height: Theme.spaceSm * 2 + Theme.fontSizeKeycap  // ~28 chip
                                        width: parent.width
                                        radius: Theme.fieldRadius
                                        color: Theme.surfaceSecondary
                                        border.color: Theme.border
                                        border.width: 1
                                        Text {
                                            id: chipLabel
                                            anchors.centerIn: parent
                                            text: modelData.text
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeKeycap
                                            font.weight: Theme.fontWeightSemibold
                                            color: Theme.textPrimary
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}