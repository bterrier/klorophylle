// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Klorophylle.Style
import Klorophylle

// Brand / about surface (reached from the nav rail's "More"): the brand header plus
// description & lineage, version/build info, license & third-party credits, and a link
// to the source. All metadata comes from the BuildInfo singleton (the generated
// version.h is the single source of version truth) — no hardcoded version in QML.
Item {
    id: root
    property string title: qsTr("About")

    // A label : value line for the version/build facts.
    component InfoRow: RowLayout {
        property alias label: key.text
        property alias value: val.text
        Layout.fillWidth: true
        spacing: Theme.spacingSm
        Label {
            id: key
            color: Theme.colorTextVariant
            Layout.fillWidth: true
        }
        Label {
            id: val
            horizontalAlignment: Text.AlignRight
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.spacingLg
        clip: true

        ColumnLayout {
            id: column
            width: Math.min(root.width - 2 * Theme.marginCompact, 480)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.spacingLg
            spacing: Theme.spacingMd

            // ---- Brand header ---------------------------------------------------
            Image {
                Layout.alignment: Qt.AlignHCenter
                source: "qrc:/klr/branding/mark.svg"   // bundled by klr_style
                sourceSize.width: Theme.fontSizeDisplay * 2
                sourceSize.height: Theme.fontSizeDisplay * 2
                fillMode: Image.PreserveAspectFit
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: BuildInfo.appName
                font.family: Theme.fontDisplay
                font.bold: true
                font.pixelSize: Theme.fontSizeHeadline
                color: Theme.colorPrimary
            }

            // ---- Description ---------------------------------------------------
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spacingSm
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("A plant-first monitoring app for Bluetooth soil & climate sensors — "
                                   + "track each plant, keep a care journal, and chart its history.")
                    }
                }
            }

            // ---- Version & build ------------------------------------------------
            SectionHeader { text: qsTr("Version"); Layout.fillWidth: true }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spacingSm
                    InfoRow { label: qsTr("Version"); value: BuildInfo.appVersion }
                    InfoRow { label: qsTr("Build"); value: BuildInfo.buildType }
                    InfoRow { label: qsTr("Qt"); value: BuildInfo.qtVersion }
                }
            }

            // ---- License & credits ----------------------------------------------
            SectionHeader { text: qsTr("License & credits"); Layout.fillWidth: true }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spacingSm
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Licensed under %1.").arg(BuildInfo.license)
                    }
                    Label {
                        Layout.fillWidth: true
                        color: Theme.colorTextVariant
                        font.pixelSize: Theme.fontSizeLabel
                        text: qsTr("Built with:")
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSizeLabel
                        text: "• Qt 6 (LGPL v3)\n"
                              + "• Montserrat & Inter fonts (SIL Open Font License)\n"
                              + "• Material Symbols (Apache License 2.0)"
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSizeLabel
                        text: qsTr("Inspired by WatchFlower by Emeric Grange (GPL v3). Some of "
                                   + "Klorophylle's code (Bluetooth sensor decoding) and data (the "
                                   + "plant catalog) are derived or reused from WatchFlower — with "
                                   + "thanks to the WatchFlower project and its contributors.\n"
                                   + "https://github.com/emericg/WatchFlower")
                    }
                }
            }

            // ---- Source ---------------------------------------------------------
            Button {
                Layout.fillWidth: true
                text: qsTr("View source on GitHub")
                onClicked: Qt.openUrlExternally(BuildInfo.sourceUrl)
            }
        }
    }
}
