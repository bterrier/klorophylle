// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// The selected sensor's live broadcast values, updating as advertisements arrive.
Item {
    id: root
    property string title: AppContext.selectedName

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        spacing: Theme.spacingBase

        Label {
            text: AppContext.selectedName
            font.pixelSize: Theme.fontSizeTitle
            elide: Label.ElideRight
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("%1 · %2 dBm").arg(AppContext.selectedId).arg(AppContext.selectedRssi)
            color: Theme.colorTextVariant
            elide: Label.ElideRight
            Layout.fillWidth: true
        }

        // Live connectivity status: a green/amber/red dot + label, the authoritative battery
        // (from the reading store, so it shows even for connect-only sensors like Flower Care),
        // when it was last heard, and a "connected" note while a GATT connection is open.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Theme.spacingSm
                implicitHeight: Theme.spacingSm
                radius: width / 2
                visible: AppContext.selectedLiveness >= 0
                color: Theme.livenessColor(AppContext.selectedLiveness)
            }
            Label {
                text: {
                    let parts = [];
                    if (AppContext.selectedGattOpen) parts.push(qsTr("connected"));
                    if (AppContext.selectedBatteryText.length > 0)
                        parts.push(qsTr("battery %1").arg(AppContext.selectedBatteryText));
                    if (AppContext.selectedLastSeenText.length > 0)
                        parts.push(qsTr("last seen %1").arg(AppContext.selectedLastSeenText));
                    return parts.join(" · ");
                }
                visible: text.length > 0
                color: AppContext.selectedGattOpen ? Theme.colorAI : Theme.colorTextVariant
                elide: Label.ElideRight
                Layout.fillWidth: true
            }
        }

        // History backfill: when this install last downloaded stored history from the sensor
        // (devices like Flower Care that keep an on-board log). Hidden until the first sync.
        Label {
            visible: AppContext.selectedLastSyncText.length > 0
            text: qsTr("History last synced %1").arg(AppContext.selectedLastSyncText)
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeCaption
            elide: Label.ElideRight
            Layout.fillWidth: true
        }

        // Non-broadcast devices carry no advertisement values: read them on demand
        // over a short GATT connection (connect -> read -> disconnect).
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingXs
            visible: AppContext.selectedCanRead
            spacing: Theme.spacingSm

            Button {
                text: AppContext.reading ? qsTr("Reading…") : qsTr("Read value")
                enabled: !AppContext.reading
                onClicked: AppContext.readValue()
            }
            BusyIndicator {
                running: AppContext.reading
                visible: AppContext.reading
                implicitWidth: 28
                implicitHeight: 28
            }
            Item { Layout.fillWidth: true }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spacingBase
            clip: true
            spacing: Theme.spacingXs
            model: AppContext.liveReadings

            delegate: ListItem {
                id: row
                width: ListView.view ? ListView.view.width : 0
                required property string label
                required property string valueText
                required property bool present

                contentItem: RowLayout {
                    spacing: Theme.spacingSm
                    Label {
                        text: row.label
                        Layout.fillWidth: true
                    }
                    Label {
                        text: row.valueText
                        font.bold: true
                        opacity: row.present ? 1.0 : 0.4
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacingMd
                visible: list.count === 0
                horizontalAlignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                text: AppContext.selectedCanRead
                    ? qsTr("No values yet.\nTap “Read value” to connect and read this sensor.")
                    : qsTr("No decodable values from this device yet.\nIt may use a format Klorophylle doesn't read yet.")
            }
        }
    }
}
