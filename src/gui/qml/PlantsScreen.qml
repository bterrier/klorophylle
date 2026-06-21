// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// The home screen: the user's plants. A plant exists with no sensor at all — it is
// added and journalled on its own (plant-first, goal #1). No presentation logic
// here; the list fields come from C++ roles.
Item {
    id: root
    property string title: qsTr("Plants")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: qsTr("Add plant")
                onClicked: addDialog.open()
            }
            Item { Layout.fillWidth: true }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingXs
            model: AppContext.plants

            delegate: Item {
                id: row
                width: ListView.view ? ListView.view.width : 0
                implicitHeight: card.implicitHeight
                required property string plantId
                required property string displayName
                required property string species
                required property int health // CareLevel: 0 Unknown, 1 Good, 2 Attention
                required property int connectivity // Liveness: 0 Offline, 1 Stale, 2 Live; <0 none
                required property var moisture // { present, valueText, fraction, hasRange }
                required property var light

                // The rich card; presentation lives in the style component. The card
                // owns its own tap surface (a Pane consumes mouse events), so navigation is
                // driven by its clicked() signal rather than a wrapping ItemDelegate.
                PlantCard {
                    id: card
                    width: parent.width
                    displayName: row.displayName
                    species: row.species
                    health: row.health
                    connectivity: row.connectivity
                    moisture: row.moisture
                    light: row.light
                    onClicked: {
                        AppContext.selectPlant(row.plantId);
                        NavigationController.replace(NavigationController.PlantDetail);
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
                text: qsTr("No plants yet.\nTap “Add plant” to start a care journal.")
            }
        }
    }

    Dialog {
        id: addDialog
        property string chosenSpecies: ""
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spacingMd, 360)
        modal: true
        title: qsTr("Add plant")
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            nameField.clear();
            addDialog.chosenSpecies = "";
            nameField.forceActiveFocus();
        }
        onAccepted: AppContext.addPlant(nameField.text, addDialog.chosenSpecies)

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingBase
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Name (e.g. Living-room ficus)")
                onAccepted: addDialog.accept()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Label {
                    Layout.fillWidth: true
                    elide: Label.ElideRight
                    text: addDialog.chosenSpecies.length > 0
                          ? addDialog.chosenSpecies : qsTr("No species")
                    color: addDialog.chosenSpecies.length > 0
                           ? Theme.colorText : Theme.colorTextVariant
                }
                Button {
                    flat: true
                    text: addDialog.chosenSpecies.length > 0 ? qsTr("Change") : qsTr("Choose…")
                    onClicked: speciesPicker.open()
                }
            }
        }
    }

    SpeciesPickerDialog {
        id: speciesPicker
        currentKey: addDialog.chosenSpecies
        onSpeciesChosen: (key) => addDialog.chosenSpecies = key
    }
}
