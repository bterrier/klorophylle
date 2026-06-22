// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView/ScrollBar (no styled variant); styled controls below still win
import QtQuick.Layouts
import Klorophylle.Style
import Klorophylle

// History backfill: the app connects on a cadence to download the hours it was closed (and
// refresh battery, which Flower Care never broadcasts). Each connect costs sensor battery, so
// the interval bounds how often that happens.
Item {
    id: root
    // Preset history-sync intervals (hours) the cadence ComboBox offers.
    readonly property var syncIntervals: [1, 3, 6, 12, 24]

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
                    text: qsTr("Download sensor history")
                    Layout.fillWidth: true
                }
                Switch {
                    checked: Settings.historySyncEnabled
                    onToggled: Settings.historySyncEnabled = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.historySyncEnabled
                Label {
                    text: qsTr("Sync at most every")
                    Layout.fillWidth: true
                }
                ComboBox {
                    model: [qsTr("1 hour"), qsTr("3 hours"), qsTr("6 hours"),
                            qsTr("12 hours"), qsTr("24 hours")]
                    currentIndex: Math.max(0, root.syncIntervals.indexOf(Settings.historySyncIntervalHours))
                    onActivated: (index) => Settings.historySyncIntervalHours = root.syncIntervals[index]
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Button {
                    text: AppContext.historySyncing ? qsTr("Syncing history…") : qsTr("Sync history now")
                    enabled: !AppContext.historySyncing
                    onClicked: AppContext.syncHistoryNow()
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
