// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of ScrollIndicator. Rooted on the QtQuick.Templates
// type (NEVER QtQuick.Controls). A thin rounded bar that fades in while scrolling. Our
// ComboBox popup and Menu reference `ScrollIndicator { }` as a sibling of this style
// (implicit directory import) + `T.ScrollIndicator.vertical` for the attached property —
// the same pattern the built-in styles use, so the style needs NO QtQuick.Controls import
// (which would recurse, since this module IS the active style). Colour from Theme.
import QtQuick
import QtQuick.Templates as T

T.ScrollIndicator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 2

    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: width / 2
        color: Theme.colorOutline
        opacity: control.active ? 0.6 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
}
