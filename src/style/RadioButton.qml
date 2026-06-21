// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of RadioButton. Rooted on the QtQuick.Templates type
// (NEVER QtQuick.Controls). A 2px Dark-Emerald ring with an emerald dot when selected;
// the ring stays muted (outline) until checked or pressed. Every colour comes from
// Theme, so it re-themes live. Replaces the Material fallback the Duplicate-plant
// dialog's choices previously rendered with.
import QtQuick
import QtQuick.Templates as T

T.RadioButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    spacing: Theme.spacingBase
    padding: Theme.spacingXs

    font.family: Theme.fontBody
    font.pixelSize: Theme.fontSizeBody

    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: control.checked || control.down ? Theme.colorPrimary : Theme.colorOutline
        opacity: control.enabled ? 1.0 : 0.4

        Rectangle {
            width: 10
            height: 10
            anchors.centerIn: parent
            radius: width / 2
            color: Theme.colorPrimary
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.colorText
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: control.enabled ? 1.0 : 0.4
    }
}
