// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of Button. Rooted on the QtQuick.Templates type
// (NEVER QtQuick.Controls — that would self-derive and break the QML compiler). Reads
// the Theme tokens from this same module, so it re-themes live. Skeleton: a solid
// Leaf-Green primary, with `flat` giving the Dark-Emerald ghost/outline variant
// (the design system "Buttons"). Anything unimplemented falls back to the neutral Basic
// style (docs/adr/0018), never Material.
import QtQuick
import QtQuick.Templates as T

T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: Theme.spacingMd
    verticalPadding: Theme.spacingSm
    spacing: Theme.spacingBase

    font.family: Theme.fontDisplay
    font.pixelSize: Theme.fontSizeLabel
    font.bold: true

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.flat ? Theme.colorPrimary : Theme.colorOnPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: control.enabled ? 1.0 : 0.4
    }

    background: Rectangle {
        implicitHeight: 40
        radius: Theme.radius
        color: {
            if (control.flat) {
                const p = Theme.colorPrimary;
                return control.down || control.hovered ? Qt.rgba(p.r, p.g, p.b, 0.08) : "transparent";
            }
            if (control.down)
                return Qt.darker(Theme.colorGood, 1.15);
            return control.hovered ? Qt.lighter(Theme.colorGood, 1.08) : Theme.colorGood;
        }
        border.color: control.flat ? Theme.colorPrimary : "transparent"
        border.width: control.flat ? 1 : 0
        opacity: control.enabled ? 1.0 : 0.4
    }
}
