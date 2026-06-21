// SPDX-License-Identifier: GPL-3.0-or-later
// Generic gap-filler: a tonal card — pure-white surface over the cyan-white canvas, 8px
// radius, low-contrast 1px border, 24px inner padding (the design system "Cards"). Built
// on the Pane template (a Control), so children placed inside become its contentItem and
// it auto-sizes around them. A soft ambient elevation shadow is layered on
// (the design system) via RectangularShadow behind the surface — every value a token.
import QtQuick
import QtQuick.Templates as T
import QtQuick.Effects

T.Pane {
    id: root

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: Theme.spacingMd

    background: Item {
        // Soft ambient shadow — drawn behind the surface, offset down + blurred (CSS
        // box-shadow `0 10px 30px`); RectangularShadow needs no layer/source, so no
        // clipping/padding fragility.
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
            color: Theme.colorCard
            radius: Theme.radius
            border.width: 1
            border.color: Theme.colorCardBorder
        }
    }
}
