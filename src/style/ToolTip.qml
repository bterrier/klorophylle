// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of ToolTip. Rooted on the QtQuick.Templates type
// (NEVER QtQuick.Controls). A Dark-Emerald surface with onPrimary caption text and a
// small radius, centred just above its parent by default. Every colour comes from Theme,
// so it re-themes live.
//
// Because main.cpp selects this style at RUNTIME (QQuickStyle::setStyle("Klorophylle.Style")),
// this delegate backs BOTH the explicit `ToolTip { }` form AND the attached form
// (`ToolTip.text: ...`, as used by ScanScreen) — the attached ToolTip's shared instance is
// drawn by the active style. See docs/adr/0018.
import QtQuick
import QtQuick.Templates as T

T.ToolTip {
    id: control

    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - Theme.spacingXs

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: Theme.spacingBase
    padding: Theme.spacingBase
    horizontalPadding: Theme.spacingSm

    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontSizeCaption
        color: Theme.colorOnPrimary
        wrapMode: Text.WordWrap
    }

    background: Rectangle {
        color: Theme.colorPrimary
        radius: Theme.radiusSm
    }
}
