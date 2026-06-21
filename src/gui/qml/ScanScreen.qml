// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // attached ToolTip type (delegate comes from the active style — ours)
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// Nearby Bluetooth devices. Scanning is always-on — started at the composition root and
// run for the app's whole lifetime (ADR 0011), so this screen is just a VIEW of it (and a
// no-op startScan() on entry keeps it running). The list is supported-first (AppContext
// sorts it): recognised sensors group at the top, the rest sit dimmed under "Other
// Bluetooth devices" so they don't drown the signal. No presentation logic here —
// labels/values/ordering/supported-ness come from C++ roles.
Item {
    id: root
    property string title: qsTr("Sensors")

    Component.onCompleted: AppContext.startScan()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            BusyIndicator {
                running: AppContext.scanning
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }
            Label {
                Layout.fillWidth: true
                text: AppContext.historySyncing ? qsTr("Downloading sensor history…")
                    : (AppContext.scanning ? qsTr("Scanning for sensors…") : qsTr("Scan stopped"))
                color: Theme.colorTextVariant
            }
            // Download stored history now from reachable paired sensors. The auto path
            // also runs at startup + on the cadence; this is the manual trigger.
            ToolButton {
                icon.name: "history"
                enabled: !AppContext.historySyncing
                onClicked: AppContext.syncHistoryNow()
            }
            ToolButton {
                icon.name: "refresh"
                onClicked: AppContext.startScan()
            }
        }

        Label {
            visible: AppContext.status.length > 0
            text: AppContext.status
            color: Theme.colorBad
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingXs
            model: AppContext.devices

            // The sorted model groups supported-first; section by that flag.
            section.property: "supported"
            section.criteria: ViewSection.FullString
            section.delegate: Label {
                required property string section
                width: ListView.view ? ListView.view.width : 0
                topPadding: Theme.spacingSm
                bottomPadding: Theme.spacingXs
                text: section === "true" ? qsTr("Supported sensors")
                                         : qsTr("Other Bluetooth devices")
                color: Theme.colorPrimary
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontSizeLabel
                font.bold: true
            }

            delegate: ListItem {
                id: row
                width: ListView.view ? ListView.view.width : 0
                required property string deviceId
                required property string deviceName
                required property string model
                required property int rssi
                required property int valueCount
                required property bool supported
                required property int liveness // 0 Offline, 1 Stale, 2 Live
                required property string lastSeen
                required property string battery
                required property bool gattOpen
                // Unsupported devices recede into the background.
                opacity: supported ? 1.0 : 0.5
                onClicked: {
                    AppContext.selectDevice(row.deviceId);
                    NavigationController.replace(NavigationController.Live);
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingSm
                    Icon {
                        icon.name: row.supported ? "sensors" : "bluetooth"
                        icon.color: row.supported ? Theme.colorAI : Theme.colorTextVariant
                        icon.size: Theme.fontSizeTitle
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs / 2
                        Label {
                            text: row.model.length > 0 ? row.model
                                : (row.deviceName.length > 0 ? row.deviceName : qsTr("Unknown device"))
                            font.bold: true
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            // Always show the hardware address so two same-model sensors are
                            // distinguishable; append value count + signal when known.
                            text: row.valueCount > 0
                                  ? qsTr("%1 · %2 values · %3 dBm").arg(row.deviceId)
                                                                   .arg(row.valueCount).arg(row.rssi)
                                  : qsTr("%1 · %2 dBm").arg(row.deviceId).arg(row.rssi)
                            color: Theme.colorTextVariant
                            font.pixelSize: Theme.fontSizeCaption
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                        // Battery (when broadcast) + last-seen, with a "connected" note while
                        // a GATT connection (read / history sync) is open to this device.
                        Label {
                            visible: text.length > 0
                            text: {
                                let parts = [];
                                if (row.gattOpen) parts.push(qsTr("connected"));
                                if (row.battery.length > 0) parts.push(qsTr("battery %1").arg(row.battery));
                                if (row.lastSeen.length > 0) parts.push(row.lastSeen);
                                return parts.join(" · ");
                            }
                            color: row.gattOpen ? Theme.colorAI : Theme.colorTextVariant
                            font.pixelSize: Theme.fontSizeCaption
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    // Live connectivity dot (green/amber/red).
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: Theme.spacingSm
                        implicitHeight: Theme.spacingSm
                        radius: width / 2
                        color: Theme.livenessColor(row.liveness)
                    }
                    Icon {
                        icon.name: "chevron_right"
                        icon.size: Theme.fontSizeTitle
                        opacity: 0.4
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
                text: AppContext.scanning ? qsTr("Scanning… no devices found yet")
                                          : qsTr("No devices found.")
            }
        }

        // Registered (known) sensors — the persisted `sensors` table, bound AND unbound
        // distinct from the live scan above. Each opens a per-sensor detail page with
        // its history + a guarded delete. Hidden until at least one sensor is registered.
        Label {
            visible: registeredList.count > 0
            Layout.fillWidth: true
            topPadding: Theme.spacingSm
            text: qsTr("Registered sensors")
            color: Theme.colorPrimary
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontSizeLabel
            font.bold: true
        }

        ListView {
            id: registeredList
            visible: count > 0
            Layout.fillWidth: true
            // Sizes to its content but never crowds out the live-scan list above.
            Layout.preferredHeight: Math.min(contentHeight, root.height * 0.4)
            clip: true
            spacing: Theme.spacingXs
            model: AppContext.registeredSensors

            delegate: ListItem {
                id: regRow
                width: ListView.view ? ListView.view.width : 0
                required property string sensorId
                required property string model
                required property string address
                required property bool bound
                required property int liveness // 0 Offline, 1 Stale, 2 Live, 3 Connected; <0 unknown
                required property string battery
                required property string lastSeen
                onClicked: {
                    AppContext.selectRegisteredSensor(regRow.sensorId);
                    NavigationController.push(NavigationController.SensorDetail);
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingSm
                    Icon {
                        icon.name: "sensors"
                        icon.color: regRow.bound ? Theme.colorAI : Theme.colorTextVariant
                        icon.size: Theme.fontSizeTitle
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs / 2
                        Label {
                            text: regRow.model.length > 0 ? regRow.model : qsTr("Unknown sensor")
                            font.bold: true
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: {
                                let parts = [regRow.address];
                                if (regRow.battery.length > 0) parts.push(qsTr("battery %1").arg(regRow.battery));
                                if (regRow.lastSeen.length > 0) parts.push(regRow.lastSeen);
                                return parts.join(" · ");
                            }
                            color: Theme.colorTextVariant
                            font.pixelSize: Theme.fontSizeCaption
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    // A plant icon marks a sensor whose readings belong to a plant (so its
                    // data can't be deleted); a hover tooltip spells it out. Unbound sensors
                    // show nothing here.
                    Icon {
                        visible: regRow.bound
                        icon.name: "potted_plant"
                        icon.color: Theme.colorAI
                        icon.size: Theme.fontSizeTitle
                        HoverHandler { id: boundHover }
                        ToolTip.visible: boundHover.hovered
                        ToolTip.text: qsTr("This sensor is bound to a plant")
                    }
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        visible: regRow.liveness >= 0
                        implicitWidth: Theme.spacingSm
                        implicitHeight: Theme.spacingSm
                        radius: width / 2
                        color: Theme.livenessColor(regRow.liveness)
                    }
                    Icon {
                        icon.name: "chevron_right"
                        icon.size: Theme.fontSizeTitle
                        opacity: 0.4
                    }
                }
            }
        }
    }
}
