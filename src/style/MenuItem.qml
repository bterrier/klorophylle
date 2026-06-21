// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of MenuItem. Rooted on the QtQuick.Templates type
// (NEVER QtQuick.Controls). A flat text row whose hover/press/highlight states are the
// same soft Qt.rgba(colorPrimary) tints as ListItem and the ComboBox popup rows, so the
// NavRail "More" overflow reads as one family. Plain non-checkable, non-submenu items
// (the app's only use), so no indicator/arrow visuals are defined. Re-themes live.
import QtQuick
import QtQuick.Templates as T

T.MenuItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    leftPadding: Theme.spacingSm
    rightPadding: Theme.spacingSm
    topPadding: Theme.spacingBase
    bottomPadding: Theme.spacingBase
    spacing: Theme.spacingBase

    font.family: Theme.fontBody
    font.pixelSize: Theme.fontSizeBody

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.colorText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: control.enabled ? 1.0 : 0.4
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 36
        radius: Theme.radiusSm
        readonly property color p: Theme.colorPrimary
        color: control.down ? Qt.rgba(p.r, p.g, p.b, 0.12)
             : control.highlighted || control.hovered ? Qt.rgba(p.r, p.g, p.b, 0.08)
             : "transparent"
    }
}
