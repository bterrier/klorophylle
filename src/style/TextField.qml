// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of TextField. Rooted on the
// QtQuick.Templates type (NEVER QtQuick.Controls). Per the design system "Input fields":
// an understated cyan-white fill, an 8px-radius 1px Dark-Emerald border that GLOWS on
// focus, and NO Material underline. The glow is a soft emerald ring (a second Rectangle
// behind the field) that fades in with focus — colour from Theme.colorPrimary, so it
// re-themes live and carries no hardcoded hex.
import QtQuick
import QtQuick.Templates as T

T.TextField {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    leftPadding: Theme.spacingSm
    rightPadding: Theme.spacingSm
    topPadding: Theme.spacingBase
    bottomPadding: Theme.spacingBase

    font.family: Theme.fontBody
    font.pixelSize: Theme.fontSizeBody

    color: Theme.colorText
    placeholderTextColor: Theme.colorTextVariant
    selectionColor: Theme.colorPrimary
    selectedTextColor: Theme.colorOnPrimary
    opacity: control.enabled ? 1.0 : 0.4

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 40
        radius: Theme.radius
        color: Theme.colorCard
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.colorPrimary : Theme.colorCardBorder

        // Focus glow — a soft emerald ring that fades in on focus.
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            readonly property color p: Theme.colorPrimary
            border.width: 3
            border.color: Qt.rgba(p.r, p.g, p.b, 0.18)
            opacity: control.activeFocus ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }
}
