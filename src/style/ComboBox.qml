// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of ComboBox. Rooted on the
// QtQuick.Templates type (NEVER QtQuick.Controls). Kills the Material chrome: an
// understated cyan-white field with a 1px emerald border that goes full-emerald on
// focus/open, the Material-Symbols `expand_more` indicator (via our Icon), and a themed
// popup whose delegate rows use the same Qt.rgba(colorPrimary) hover/highlight tint as
// the other controls. Every colour comes from Theme, so it re-themes live.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    leftPadding: Theme.spacingSm
    rightPadding: indicator.width + Theme.spacingSm
    topPadding: Theme.spacingBase
    bottomPadding: Theme.spacingBase

    font.family: Theme.fontBody
    font.pixelSize: Theme.fontSizeBody

    // Active (focused or popup open) → the emerald border asserts itself.
    readonly property bool active: control.activeFocus || control.popup.visible

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Theme.colorText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: control.enabled ? 1.0 : 0.4
    }

    indicator: Icon {
        x: control.width - width - Theme.spacingSm
        y: control.topPadding + (control.availableHeight - height) / 2
        icon.name: "expand_more"
        icon.size: Theme.fontSizeTitle
        icon.color: control.active ? Theme.colorPrimary : Theme.colorTextVariant
        opacity: control.enabled ? 1.0 : 0.4
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 40
        radius: Theme.radius
        color: Theme.colorCard
        border.width: control.active ? 2 : 1
        border.color: control.active ? Theme.colorPrimary : Theme.colorCardBorder
        opacity: control.enabled ? 1.0 : 0.4
    }

    delegate: T.ItemDelegate {
        id: row
        required property var model
        required property int index

        width: ListView.view ? ListView.view.width : implicitWidth
        height: implicitContentHeight + topPadding + bottomPadding
        leftPadding: Theme.spacingSm
        rightPadding: Theme.spacingSm
        topPadding: Theme.spacingBase
        bottomPadding: Theme.spacingBase
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: row.model[control.textRole] !== undefined ? row.model[control.textRole]
                                                             : String(row.model)
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSizeBody
            color: Theme.colorText
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            readonly property color p: Theme.colorPrimary
            color: row.highlighted ? Qt.rgba(p.r, p.g, p.b, 0.10)
                 : row.hovered ? Qt.rgba(p.r, p.g, p.b, 0.06)
                 : "transparent"
        }
    }

    popup: T.Popup {
        y: control.height + Theme.spacingXs
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2 * Theme.spacingXs, 320)
        padding: Theme.spacingXs

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            T.ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.colorCard
            border.width: 1
            border.color: Theme.colorCardBorder
        }
    }
}
