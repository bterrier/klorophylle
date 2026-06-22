// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView/ScrollBar (no styled variant); styled controls below still win
import QtQuick.Layouts
import Klorophylle.Style
import Klorophylle

// Care alerts: a desktop notification when a plant crosses a care threshold — notably soil
// moisture dropping too low ("time to water"). The judgment + debounce live in C++
// (AlertController); here we only toggle and snooze.
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
                    text: qsTr("Care notifications")
                    Layout.fillWidth: true
                }
                Switch {
                    checked: Settings.notificationsEnabled
                    onToggled: Settings.notificationsEnabled = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.notificationsEnabled
                Label {
                    text: qsTr("Snooze for")
                    Layout.fillWidth: true
                }
                Button { text: qsTr("1h"); onClicked: AppContext.snoozeNotifications(1) }
                Button { text: qsTr("8h"); onClicked: AppContext.snoozeNotifications(8) }
                Button { text: qsTr("24h"); onClicked: AppContext.snoozeNotifications(24) }
            }

            // Only shown while a snooze is active (text is empty otherwise — computed in C++).
            Label {
                Layout.fillWidth: true
                visible: AppContext.notificationsSnoozedText.length > 0
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                text: AppContext.notificationsSnoozedText
            }
        }
    }
}
