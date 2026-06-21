// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ButtonGroup — not in Klorophylle.Style
import Klorophylle.Style
import QtQuick.Layouts

// Pure input dialog for "Duplicate this plant": a name (prefilled "<name> (copy)") and —
// when the plant currently shares a sensor — what the duplicate should do with it going
// forward (the repot split). The host (PlantSettingsScreen) does the work on `confirmed`:
// the dialog itself has no AppContext calls, so the logic stays in one place.
Dialog {
    id: root

    // The plant being duplicated, and whether it has any bound sensor (drives the choice).
    property string sourceName: ""
    property bool hasSensor: false

    // sensorAction: 0 = keep sharing, 1 = detach (pair later), 2 = pair a new sensor now.
    signal confirmed(string newName, int sensorAction)

    anchors.centerIn: parent
    width: Math.min(parent.width - 2 * Theme.spacingMd, 400)
    height: Math.min(parent.height - 2 * Theme.spacingMd, root.hasSensor ? 360 : 220)
    modal: true
    title: qsTr("Duplicate plant")
    standardButtons: Dialog.Cancel | Dialog.Ok

    // Reset on each open so a reused instance never shows the previous plant's name/choice.
    onAboutToShow: {
        nameField.text = root.sourceName + qsTr(" (copy)");
        keep.checked = true;
    }
    onAccepted: root.confirmed(nameField.text,
                               root.hasSensor ? (detach.checked ? 1 : (pairNew.checked ? 2 : 0)) : 0)

    ButtonGroup { id: sensorChoice }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSm

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeLabel
            text: qsTr("Creates a second plant with the same species, care thresholds, journal and full history — so a once-shared pot can be tracked as two.")
        }

        Label { text: qsTr("Name"); color: Theme.colorTextVariant; font.pixelSize: Theme.fontSizeCaption }
        TextField {
            id: nameField
            Layout.fillWidth: true
            font.pixelSize: Theme.fontSizeBody
        }

        // The repot step: what the duplicate does with the sensor it currently shares.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSm
            visible: root.hasSensor
            spacing: Theme.spacingXs

            Label {
                text: qsTr("Sensor")
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
            }
            RadioButton {
                id: keep
                checked: true
                ButtonGroup.group: sensorChoice
                text: qsTr("Keep sharing the current sensor")
            }
            RadioButton {
                id: detach
                ButtonGroup.group: sensorChoice
                text: qsTr("Remove the sensor (pair one later)")
            }
            RadioButton {
                id: pairNew
                ButtonGroup.group: sensorChoice
                text: qsTr("Pair a new sensor now")
            }
        }
    }
}
