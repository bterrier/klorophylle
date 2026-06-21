// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of Menu. Rooted on the QtQuick.Templates type (NEVER
// QtQuick.Controls). Same card chrome as Dialog — a white surface with the 1px outline
// rule, a rounded corner, and the soft ambient RectangularShadow elevation token — so a
// popped menu floats coherently. Rows resolve to our themed MenuItem. Every colour comes
// from Theme, so it re-themes live.
import QtQuick
import QtQuick.Templates as T
import QtQuick.Effects

T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    margins: 0
    overlap: 0
    padding: Theme.spacingXs

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window
                     ? contentHeight + control.topPadding + control.bottomPadding > control.height
                     : false
        clip: true
        currentIndex: control.currentIndex
        T.ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: Item {
        implicitWidth: 180
        // Soft ambient elevation (the design system) — the menu floats above content.
        RectangularShadow {
            anchors.fill: surface
            radius: surface.radius
            blur: Theme.elevationBlur
            offset.y: Theme.elevationOffsetY
            color: Theme.colorShadow
        }
        Rectangle {
            id: surface
            anchors.fill: parent
            radius: Theme.radius
            color: Theme.colorCard
            border.width: 1
            border.color: Theme.colorCardBorder
        }
    }
}
