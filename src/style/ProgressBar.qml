// SPDX-License-Identifier: GPL-3.0-or-later
// The signature metric bar (the design system "Progress bars"): a pill track with a
// Leaf Green → Cyan gradient fill (nature→tech synthesis). `value` is 0..1. Built on the
// ProgressBar template like the other control overrides; every colour comes from Theme
// (the gradient endpoints are the existing colorGood/colorAI tokens — colorAI is the
// sanctioned cyan path — so it re-themes live; the formal gradient/elevation TOKENS land
// with the rest of Slice C). Reused by PlantCard and, later, the plant-detail readouts.
import QtQuick
import QtQuick.Templates as T

T.ProgressBar {
    id: control

    implicitWidth: 120
    implicitHeight: Theme.spacingBase // 8px pill

    background: Rectangle {
        radius: height / 2
        color: Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.12)
    }

    contentItem: Item {
        Rectangle {
            width: Math.max(height, control.visualPosition * parent.width)
            height: parent.height
            radius: height / 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.colorGood }
                GradientStop { position: 1.0; color: Theme.colorAI }
            }
        }
    }
}
