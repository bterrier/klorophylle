// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of TabBar. Rooted on the
// QtQuick.Templates type (NEVER QtQuick.Controls). A flat, transparent strip sitting on
// the light content header; the active-tab emerald indicator lives on the TabButton, so
// this only lays the buttons out and provides a hairline baseline. Pairs with
// TabButton.qml.
import QtQuick
import QtQuick.Templates as T

T.TabBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    spacing: Theme.spacingSm

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
    }

    background: Rectangle {
        color: "transparent"
        // Hairline baseline under the whole bar.
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.colorCardBorder
        }
    }
}
