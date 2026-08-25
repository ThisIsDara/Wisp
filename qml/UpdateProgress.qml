import QtQuick
import QtQuick.Window
import wisp

// Phase 8 download progress (UI-SPEC S3): small dark always-on-top window,
// determinate accent bar (bytes received / asset size). Hosted by
// UpdateDialogs: `progressLabel` (string) and `progressRatio` (real 0..1)
// are pushed from C++. No buttons, no close box.
Window {
    id: root
    title: "wisp update"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    width: Theme.updateProgressWidth
    height: Theme.updateProgressHeight
    color: "transparent"
    visible: false

    property var updateUi: null
    property string progressLabel: "Downloading wisp..."
    property real progressRatio: 0

    Image {
        anchors.fill: parent
        source: "assets/shadow.png"
        opacity: Theme.shadowOpacity
    }

    Rectangle {
        anchors.centerIn: parent
        width: Theme.updateProgressWidth - 32
        height: Theme.updateProgressHeight - 32
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
                text: root.progressLabel
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSubtitle
                font.weight: Theme.fontWeightSemibold
                color: Theme.textPrimary
                elide: Text.ElideRight
            }

            // Determinate bar - track surfaceSecondary, fill accent
            // (scanBar tokens; the indeterminate sweep stays scan-only).
            Rectangle {
                width: parent.width
                height: Theme.scanBarHeight
                radius: Theme.scanBarRadius
                color: Theme.surfaceSecondary

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    width: Math.max(0, Math.min(1, root.progressRatio)) * parent.width
                    radius: Theme.scanBarRadius
                    color: Theme.accent
                }
            }
        }
    }
}
