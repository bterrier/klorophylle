// SPDX-License-Identifier: GPL-3.0-or-later
// Generic gap-filler: a pill status chip (full radius = "fluidity", the design system
// "Chips"). Built on the Control template so it auto-sizes around its label. `pillColor`
// is a semantic Theme colour (good/warn/bad/ai); the fill is a soft tint of it.
//
// Compact mode: when space is tight a consumer sets `compact: true` and the chip
// collapses to a circular disk of the SOLID `pillColor` with `iconName` knocked out in
// the canvas colour — the text label yields its width. `fullWidth` reports the text-mode
// width regardless of `compact` (the label is always laid out, just hidden), so a parent
// can decide whether the text form fits without a binding feedback loop.
import QtQuick
import QtQuick.Templates as T

T.Control {
    id: root
    property alias text: label.text
    property color pillColor: Theme.colorGood
    property string iconName     // shown instead of the text in compact mode
    property bool compact: false

    readonly property real fullWidth: label.implicitWidth + 2 * Theme.spacingSm

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    // Equal padding in compact mode keeps the disk square (→ circular at radiusFull).
    horizontalPadding: root.compact ? verticalPadding : Theme.spacingSm
    verticalPadding: Theme.spacingXs

    contentItem: Item {
        implicitWidth: root.compact ? Theme.fontSizeBody : label.implicitWidth
        implicitHeight: root.compact ? Theme.fontSizeBody : label.implicitHeight
        Text {
            id: label
            anchors.centerIn: parent
            visible: !root.compact
            color: root.pillColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSizeLabel
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Icon {
            anchors.centerIn: parent
            visible: root.compact
            icon.name: root.iconName
            icon.size: Theme.fontSizeBody
            icon.color: Theme.colorBackground // knockout — reads on the solid disk, both schemes
        }
    }

    background: Rectangle {
        radius: Theme.radiusFull
        color: root.compact ? root.pillColor
                            : Qt.rgba(root.pillColor.r, root.pillColor.g, root.pillColor.b, 0.15)
    }
}
