// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// The app's primary navigation — a dark-emerald "authority" sidebar (the design system:
// primary = nav containers/branding). Two top-level sections (Plants, Sensors) plus a
// "More" overflow for the secondary screens. Icon+label rail; the active section is
// highlighted. Drives NavigationController directly (the app's single nav source of
// truth); the StackView in Main.qml mirrors it.
Rectangle {
    id: rail
    implicitWidth: 96
    color: Theme.colorPrimary

    // One rail entry: stacked icon over label, tinted onPrimary, with an active/hover
    // highlight behind it.
    component RailButton: ListItem {
        id: ctl
        property alias iconName: glyph.icon.name
        property string label
        property bool active: false

        Layout.fillWidth: true
        implicitHeight: 68
        padding: Theme.spacingXs

        contentItem: ColumnLayout {
            spacing: 2
            Icon {
                id: glyph
                Layout.alignment: Qt.AlignHCenter
                icon.color: Theme.colorOnPrimary
                icon.size: Theme.fontSizeHeadline
                opacity: ctl.active ? 1.0 : 0.75
            }
            Label {
                text: ctl.label
                Layout.alignment: Qt.AlignHCenter
                color: Theme.colorOnPrimary
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSizeCaption
                font.bold: ctl.active
                opacity: ctl.active ? 1.0 : 0.75
            }
        }

        background: Rectangle {
            radius: Theme.radius
            anchors.fill: parent
            anchors.margins: Theme.spacingXs
            readonly property color tint: Theme.colorOnPrimary
            color: ctl.active ? Qt.rgba(tint.r, tint.g, tint.b, 0.18)
                 : ctl.hovered ? Qt.rgba(tint.r, tint.g, tint.b, 0.09)
                 : "transparent"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.spacingMd
        anchors.bottomMargin: Theme.spacingMd
        spacing: Theme.spacingBase

        Icon { // brand mark
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.spacingBase
            icon.name: "eco"
            icon.color: Theme.colorOnPrimary
            icon.size: Theme.fontSizeDisplay / 2
        }

        RailButton {
            iconName: "potted_plant"
            label: qsTr("Plants")
            // Not highlighted while a full-width page (AI/Settings/…) covers the section.
            active: NavigationController.currentSection === NavigationController.Plants
                    && !NavigationController.currentIsFullPage
            onClicked: NavigationController.goSection(NavigationController.Plants)
        }
        RailButton {
            iconName: "sensors"
            label: qsTr("Sensors")
            active: NavigationController.currentSection === NavigationController.Sensors
                    && !NavigationController.currentIsFullPage
            onClicked: NavigationController.goSection(NavigationController.Sensors)
        }
        RailButton {
            // The AI assistant — a top-level destination, not buried in More. It is a
            // full-width page, so it can't be a master/detail section (goSection); goPage() opens
            // it as a top-level page that collapses the detail instead of STACKING on top of an
            // open plant/sensor detail (push, the More-overflow behaviour). active tracks the route.
            iconName: "auto_awesome"
            label: qsTr("AI")
            active: NavigationController.currentRoute === NavigationController.AIInsights
            onClicked: NavigationController.goPage(NavigationController.AIInsights)
        }

        Item { Layout.fillHeight: true } // push "More" to the bottom

        RailButton {
            iconName: "more_horiz"
            label: qsTr("More")
            onClicked: moreMenu.popup()
        }
    }

    Menu {
        id: moreMenu
        MenuItem { text: qsTr("Settings"); onTriggered: NavigationController.push(NavigationController.Settings) }
        MenuItem { text: qsTr("Export data"); onTriggered: NavigationController.push(NavigationController.Export) }
        MenuItem { text: qsTr("About"); onTriggered: NavigationController.push(NavigationController.About) }
    }
}
