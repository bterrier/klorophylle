// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView/ScrollBar (no styled variant); styled controls below still win
import QtQuick.Layouts
import Klorophylle.Style

// Appearance settings: the colour scheme. Binds the persisted `Settings` singleton; Main.qml
// drives the live Theme from it. (One category page of the Settings index — see SettingsCategoryScreen.)
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
                    text: qsTr("Colour scheme")
                    Layout.fillWidth: true
                }
                ComboBox {
                    // SettingsStore persists the choice; Main.qml binds Theme to it.
                    // Order matches ThemeController.ColorScheme: 0 = Light, 1 = Dark, 2 = Auto.
                    model: [qsTr("Light"), qsTr("Dark"), qsTr("Auto")]
                    currentIndex: Settings.colorScheme
                    onActivated: (index) => Settings.colorScheme = index
                }
            }
        }
    }
}
