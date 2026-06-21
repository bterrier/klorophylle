// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of ToolBar. Rooted on the QtQuick.Templates type.
// A LIGHT content header (back + page title) sitting beside the dark-emerald NavRail.
// The brand "authority" surface is the NavRail sidebar (colorPrimary + onPrimary), not
// this bar — so the header stays a light surface with dark-emerald children (see
// docs/adr/0007 item #11). Header children that need an on-dark tint still have the
// `ToolButton.contentColor` seam available.
import QtQuick
import QtQuick.Templates as T

T.ToolBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: Theme.spacingBase

    background: Rectangle {
        implicitHeight: 56
        color: Theme.colorSurface

        Rectangle { // bottom hairline
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.colorCardBorder
        }
    }
}
