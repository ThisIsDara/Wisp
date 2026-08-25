import QtQuick
import QtQuick.Window
import wisp

// Phase 8 update prompt (UI-SPEC S2): Download now / Later. Hosted by
// UpdateDialogs (HotkeyCaptureDialog precedent): per-instance beginCreate/
// setProperty injects this window's `updateUi` host and `updateVersion`.
// Esc = Later. No close animation beyond the shell fade tokens.
Window {
    id: root
    title: "wisp update"
    flags: Qt.Tool | Qt.FramelessWindowHint
    width: Theme.updateDialogWidth
    height: Theme.updateDialogHeight
    color: "transparent"
    visible: false

    property var updateUi: null
    property string updateVersion: ""

    function accept() {
        if (updateUi)
            updateUi.accept()
    }

    function dismiss() {
        if (updateUi)
            updateUi.dismiss()
    }

    Keys.onEscapePressed: (event) => { root.dismiss(); event.accepted = true }

    Image {
        anchors.fill: parent
        source: "assets/shadow.png"
        opacity: Theme.shadowOpacity
    }

    Rectangle {
        anchors.centerIn: parent
        width: Theme.updateDialogWidth - 32   // inside the shadow margin
        height: Theme.updateDialogHeight - 32
        radius: Theme.radiusSurface
        color: Theme.surface
        border.color: Theme.border
        border.width: 1

        Column {
            anchors.fill: parent
            anchors.margins: Theme.spaceLg
            spacing: Theme.spaceMd

            Text {
                width: parent.width
                text: root.updateVersion === ""
                      ? "Update available"
                      : "Update to wisp v" + root.updateVersion + " available"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightSemibold
                color: Theme.textPrimary
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: "The installer runs silently and wisp restarts automatically."
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSubtitle
                color: Theme.textSecondary
            }

            Row {
                spacing: Theme.spaceSm
                anchors.right: parent.right

                Rectangle {
                    width: 84
                    height: Theme.settingsRowScanItem
                    radius: Theme.fieldRadius
                    color: dlHover.containsMouse ? Theme.accentDark : Theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: "Download now"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightSemibold
                        color: Theme.onAccentText
                    }
                    MouseArea {
                        id: dlHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.accept()
                    }
                }
                Rectangle {
                    width: 84
                    height: Theme.settingsRowScanItem
                    radius: Theme.fieldRadius
                    color: Theme.surfaceSecondary
                    border.color: Theme.border
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "Later"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightSemibold
                        color: Theme.textPrimary
                    }
                    MouseArea {
                        id: laterHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.dismiss()
                    }
                }
            }
        }
    }
}
