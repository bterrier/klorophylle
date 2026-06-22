// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView/ScrollBar (no styled variant); styled controls below still win
import QtQuick.Layouts
import Klorophylle.Style

// Display-unit preferences. Each control binds the persisted `Settings` singleton — no logic
// here; the actual conversion happens in C++ at the display boundary (storage stays canonical).
Item {
    id: root

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        clip: true
        contentWidth: availableWidth // never scroll horizontally
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.spacingMd

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Label {
                    text: qsTr("Temperature")
                    Layout.fillWidth: true
                }
                ComboBox {
                    // klr::TemperatureUnit: 0 = Celsius, 1 = Fahrenheit.
                    model: ["°C", "°F"]
                    currentIndex: Settings.temperatureUnit
                    onActivated: (index) => Settings.temperatureUnit = index
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Label {
                    text: qsTr("Light")
                    Layout.fillWidth: true
                }
                ComboBox {
                    // klr::IlluminanceUnit: 0 = Lux, 1 = Micromole (µmol).
                    model: ["lux", "µmol"]
                    currentIndex: Settings.illuminanceUnit
                    onActivated: (index) => Settings.illuminanceUnit = index
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Label {
                    text: qsTr("Pressure")
                    Layout.fillWidth: true
                }
                ComboBox {
                    // klr::PressureUnit: 0 = Hectopascal, 1 = InchHg, 2 = MmHg.
                    model: ["hPa", "inHg", "mmHg"]
                    currentIndex: Settings.pressureUnit
                    onActivated: (index) => Settings.pressureUnit = index
                }
            }
        }
    }
}
