// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of TextArea — the multi-line sibling of
// TextField.qml. Rooted on the QtQuick.Templates type (NEVER QtQuick.Controls).
// Shares TextField's visual language: an understated cyan-white fill, an
// 8px-radius 1px Dark-Emerald border that GLOWS on focus (a soft emerald ring
// behind the field that fades in), and NO Material underline. Wraps text and
// grows with content so a long note (e.g. an imported report) is editable as a
// block rather than a single scrolling line.
import QtQuick
import QtQuick.Templates as T

T.TextArea {
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

    wrapMode: TextEdit.Wrap

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 96
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
