// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of ToolButton. Rooted on the QtQuick.Templates type.
// It bridges the control's built-in `icon` grouped property (Qt's QQuickIcon: name,
// source, color, width) to our Icon renderer, so call sites write `ToolButton {
// icon.name: "delete" }` instead of overriding contentItem. Falls back to a text label
// when no icon is set.
import QtQuick
import QtQuick.Templates as T

T.ToolButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: Theme.spacingBase
    spacing: Theme.spacingBase

    font.family: Theme.fontBody
    font.pixelSize: Theme.fontSizeLabel
    font.bold: true

    readonly property bool hasIcon: control.icon.name.length > 0
                                    || control.icon.source.toString().length > 0

    // The colour of the icon/text + the hover/press ripple. Defaults to the emerald
    // primary (correct on light screens); the dark nav bar overrides it to
    // `Theme.colorOnPrimary` so its children read white. See ToolBar.qml / adr 0007.
    property color contentColor: Theme.colorPrimary

    contentItem: Item {
        implicitWidth: control.hasIcon ? glyph.implicitWidth : label.implicitWidth
        implicitHeight: control.hasIcon ? glyph.implicitHeight : label.implicitHeight

        Icon {
            id: glyph
            anchors.centerIn: parent
            visible: control.hasIcon
            icon.name: control.icon.name
            icon.source: control.icon.source
            icon.color: control.icon.color.a > 0 ? control.icon.color : control.contentColor
            icon.size: control.icon.width > 0 ? control.icon.width : Theme.fontSizeTitle
            opacity: control.enabled ? 1.0 : 0.4
        }
        Text {
            id: label
            anchors.centerIn: parent
            visible: !control.hasIcon
            text: control.text
            font: control.font
            color: control.contentColor
            opacity: control.enabled ? 1.0 : 0.4
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        implicitWidth: 40
        implicitHeight: 40
        radius: Theme.radius
        readonly property color tint: control.contentColor
        color: control.down ? Qt.rgba(tint.r, tint.g, tint.b, 0.16)
             : control.hovered ? Qt.rgba(tint.r, tint.g, tint.b, 0.08)
             : "transparent"
    }
}
