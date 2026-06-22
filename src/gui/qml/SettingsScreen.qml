// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView/ScrollBar (no styled variant)
import QtQuick.Layouts
import Klorophylle.Style
import Klorophylle

// The Settings index (reached from the nav rail's "More"): one tappable row per category,
// each pushing the single parameterized SettingsCategory route with its `which` token. The
// category pages themselves live in SettingsCategoryScreen + the per-category body files.
// Export and About stay their own routes in the "More" overflow, not categories here.
Item {
    id: root
    property string title: qsTr("Settings")

    // The categories, in display order. `which` is the routing token; `icon`/`label`/`detail`
    // are presentation only — the bodies (and their bindings) live in the category page.
    readonly property var categories: [
        { which: "appearance", icon: "palette", label: qsTr("Appearance"),
          detail: qsTr("Colour scheme") },
        { which: "units", icon: "straighten", label: qsTr("Units"),
          detail: qsTr("Temperature, light, pressure") },
        { which: "notifications", icon: "notifications", label: qsTr("Notifications"),
          detail: qsTr("Care alerts and snooze") },
        { which: "sensors", icon: "sensors", label: qsTr("Sensors"),
          detail: qsTr("History download cadence") },
        { which: "ai", icon: "auto_awesome", label: qsTr("AI assistant"),
          detail: qsTr("Provider, model, tools, key") },
        { which: "data", icon: "database", label: qsTr("Data"),
          detail: qsTr("Import a WatchFlower database") }
    ]

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        clip: true
        contentWidth: availableWidth // never scroll horizontally
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.spacingXs

            Repeater {
                model: root.categories
                delegate: ListItem {
                    id: categoryRow
                    required property var modelData
                    Layout.fillWidth: true
                    onClicked: NavigationController.push(NavigationController.SettingsCategory,
                                                        { which: categoryRow.modelData.which })
                    contentItem: RowLayout {
                        spacing: Theme.spacingSm
                        Icon {
                            icon.name: categoryRow.modelData.icon
                            icon.color: Theme.colorPrimary
                            icon.size: Theme.fontSizeTitle
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXs / 2
                            Label {
                                text: categoryRow.modelData.label
                                font.bold: true
                                Layout.fillWidth: true
                            }
                            Label {
                                text: categoryRow.modelData.detail
                                color: Theme.colorTextVariant
                                font.pixelSize: Theme.fontSizeCaption
                                elide: Label.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Icon {
                            icon.name: "chevron_right"
                            icon.color: Theme.colorTextVariant
                            icon.size: Theme.fontSizeTitle
                        }
                    }
                }
            }
        }
    }
}
