// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of TabButton. Rooted on the
// QtQuick.Templates type (NEVER QtQuick.Controls). The checked tab reads in the emerald
// authority colour with a 2px emerald underline indicator; idle tabs are muted. Hover/
// press are soft Qt.rgba(colorPrimary) tints — no Material ripple. Pairs with TabBar.qml.
import QtQuick
import QtQuick.Templates as T

T.TabButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: Theme.spacingMd
    verticalPadding: Theme.spacingSm
    spacing: Theme.spacingBase

    font.family: Theme.fontDisplay
    font.pixelSize: Theme.fontSizeLabel
    font.bold: true

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? Theme.colorPrimary : Theme.colorTextVariant
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: control.enabled ? 1.0 : 0.4
    }

    background: Rectangle {
        implicitHeight: 40
        readonly property color p: Theme.colorPrimary
        color: control.down ? Qt.rgba(p.r, p.g, p.b, 0.12)
             : control.hovered ? Qt.rgba(p.r, p.g, p.b, 0.06)
             : "transparent"

        // Active-tab emerald underline indicator.
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 2
            color: Theme.colorPrimary
            visible: control.checked
        }
    }
}
