// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of a list row. Rooted on the
// QtQuick.Templates ItemDelegate (NEVER QtQuick.Controls). This is STRUCTURAL only — it
// themes the row's chrome (padding + hover/press/highlight background, no Material
// ripple) and leaves `contentItem` entirely to the caller, since the app's rows compose
// wildly different content (a StatusPill + value, a delete ToolButton, a device line, an
// action row…). All state colours are soft Qt.rgba(colorPrimary) tints, so the row
// re-themes live and carries no hardcoded hex.
import QtQuick
import QtQuick.Templates as T

T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    leftPadding: Theme.spacingSm
    rightPadding: Theme.spacingSm
    topPadding: Theme.spacingBase
    bottomPadding: Theme.spacingBase
    spacing: Theme.spacingSm

    background: Rectangle {
        radius: Theme.radius
        readonly property color p: Theme.colorPrimary
        color: control.down ? Qt.rgba(p.r, p.g, p.b, 0.12)
             : control.highlighted ? Qt.rgba(p.r, p.g, p.b, 0.10)
             : control.hovered ? Qt.rgba(p.r, p.g, p.b, 0.06)
             : "transparent"
    }
}
