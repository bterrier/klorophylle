// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import Klorophylle

// A single Settings category page (Appearance/Units/Notifications/Sensors/AI/Data), reached
// from the SettingsScreen index via push(SettingsCategory, {which:"…"}) — one parameterized
// route, not one route per category. `which` selects the body to load; `title` (read by the
// shell's detail ToolBar) follows it. The AI screen deep-links here with {which:"ai"}.
Item {
    id: root

    // The category to show, set by the pusher (the Settings index row, or the AI screen).
    required property string which

    // Shown in the shell's detail ToolBar — see Main.qml (stack.currentItem?.title).
    readonly property string title: ({
        "appearance": qsTr("Appearance"),
        "units": qsTr("Units"),
        "notifications": qsTr("Notifications"),
        "sensors": qsTr("Sensors"),
        "ai": qsTr("AI assistant"),
        "data": qsTr("Data")
    }[which] ?? qsTr("Settings"))

    Loader {
        anchors.fill: parent
        sourceComponent: {
            switch (root.which) {
            case "appearance": return appearanceBody;
            case "units": return unitsBody;
            case "notifications": return notificationBody;
            case "sensors": return sensorBody;
            case "ai": return aiBody;
            case "data": return dataBody;
            }
            return appearanceBody;
        }
    }

    Component { id: appearanceBody; AppearanceSettings {} }
    Component { id: unitsBody; UnitsSettings {} }
    Component { id: notificationBody; NotificationSettings {} }
    Component { id: sensorBody; SensorSettings {} }
    Component { id: aiBody; AiSettings {} }
    Component { id: dataBody; DataSettings {} }
}
