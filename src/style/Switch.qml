// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of Switch. Rooted on the QtQuick.Templates type
// (NEVER QtQuick.Controls — that would self-derive and break the QML compiler). A pill
// track that fills Leaf-Green when on, with a white handle that slides; off it is a soft
// emerald tint with a 1px border. Every colour comes from Theme, so it re-themes live.
// Replaces the Material fallback the two Settings toggles previously rendered with.
import QtQuick
import QtQuick.Templates as T

T.Switch {
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
        implicitWidth: 44
        implicitHeight: 24
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        readonly property color p: Theme.colorPrimary
        color: control.checked ? Theme.colorGood : Qt.rgba(p.r, p.g, p.b, 0.12)
        border.width: control.checked ? 0 : 1
        border.color: Theme.colorCardBorder
        opacity: control.enabled ? 1.0 : 0.4
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            width: 20
            height: 20
            radius: width / 2
            x: control.checked ? parent.width - width - 2 : 2
            y: (parent.height - height) / 2
            color: Theme.colorCard
            border.width: control.checked ? 0 : 1
            border.color: Theme.colorCardBorder
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
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
