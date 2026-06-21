// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of DialogButtonBox. Rooted on the
// QtQuick.Templates type (NEVER QtQuick.Controls). It is the footer button bar of our
// themed Dialog: a transparent strip (no Material separator/elevation) that lays the
// standard buttons out right-aligned. Its button `delegate` is left at the default, so
// the buttons resolve to our themed Button — the whole footer reads as the brand.
import QtQuick
import QtQuick.Templates as T

T.DialogButtonBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    spacing: Theme.spacingSm
    padding: Theme.spacingSm
    alignment: Qt.AlignRight

    contentItem: ListView {
        model: control.contentModel
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        snapMode: ListView.SnapToItem
        implicitWidth: contentWidth
    }

    background: Rectangle {
        color: "transparent"
    }
}
