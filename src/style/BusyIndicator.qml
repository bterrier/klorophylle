// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of BusyIndicator. Rooted on the QtQuick.Templates
// type (NEVER QtQuick.Controls). A ring of eight Dark-Emerald dots that fade around the
// circle as the whole ring rotates. Deliberately emerald/primary, NOT cyan — cyan is
// reserved for AI/sensor accents (the design system brand rule), and a generic spinner is
// neither. Every colour comes from Theme, so it re-themes live.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T

T.BusyIndicator {
    id: control

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding

    padding: Theme.spacingXs

    contentItem: Item {
        implicitWidth: 32
        implicitHeight: 32
        opacity: control.running ? 1.0 : 0.0
        Behavior on opacity { OpacityAnimator { duration: 200 } }

        Item {
            id: spinner
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height)
            height: width

            RotationAnimator {
                target: spinner
                running: control.running && control.visible
                from: 0
                to: 360
                duration: 1200
                loops: Animation.Infinite
            }

            Repeater {
                model: 8
                delegate: Rectangle {
                    required property int index
                    readonly property real dotSize: spinner.width * 0.16
                    readonly property real ringRadius: spinner.width / 2 - dotSize / 2
                    width: dotSize
                    height: dotSize
                    radius: dotSize / 2
                    color: Theme.colorPrimary
                    opacity: 0.25 + 0.75 * (index / 7)
                    x: spinner.width / 2 - dotSize / 2 + Math.cos(index / 8 * 2 * Math.PI) * ringRadius
                    y: spinner.height / 2 - dotSize / 2 + Math.sin(index / 8 * 2 * Math.PI) * ringRadius
                }
            }
        }
    }
}
