// SPDX-License-Identifier: GPL-3.0-or-later
// The cyan AI/sensor accent (the design system "AI indicators"): a small pulsing node —
// the logo's neural-node motif. This is the ONE reusable home for Theme.colorAI as an
// indicator, so the brand's "cyan = AI/sensor only" rule is encapsulated and cyan never
// leaks into generic chrome. Minimal by design and has NO consumer yet — the AI surfaces
// that use it arrive with the AI agent; the component lands now so the path exists.
import QtQuick

Item {
    id: root

    property real coreSize: Theme.spacingSm    // solid-dot diameter (12px)
    property color color: Theme.colorAI         // sanctioned cyan
    property bool running: true                 // pause the pulse (e.g. when offscreen)

    implicitWidth: coreSize * 2.4
    implicitHeight: coreSize * 2.4

    // Halo — scales out and fades on a loop to read as a soft pulse.
    Rectangle {
        id: halo
        anchors.centerIn: parent
        width: root.coreSize
        height: root.coreSize
        radius: width / 2
        color: root.color
        opacity: 0.0

        ParallelAnimation {
            running: root.running
            loops: Animation.Infinite
            NumberAnimation { target: halo; property: "scale"; from: 1.0; to: 2.4
                              duration: 1400; easing.type: Easing.OutCubic }
            NumberAnimation { target: halo; property: "opacity"; from: 0.45; to: 0.0
                              duration: 1400; easing.type: Easing.OutCubic }
        }
    }

    // Solid core.
    Rectangle {
        anchors.centerIn: parent
        width: root.coreSize
        height: root.coreSize
        radius: width / 2
        color: root.color
    }
}
