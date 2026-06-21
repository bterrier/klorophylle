// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// Advanced / destructive actions for the selected plant, kept off the main detail
// view: sensor pairing (attach/detach) and deleting the plant. Reached via the gear
// on PlantDetailScreen.
Item {
    id: root
    property string title: AppContext.selectedPlantName.length > 0
                           ? qsTr("%1 · Settings").arg(AppContext.selectedPlantName)
                           : qsTr("Plant settings")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        spacing: Theme.spacingSm

        // ---- Species ---- pick (or clear) the catalog entry this plant is. Setting
        // it re-seeds the care thresholds below from the species' ideal ranges.
        SectionHeader {
            text: qsTr("Species")
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Label {
                Layout.fillWidth: true
                elide: Label.ElideRight
                text: AppContext.selectedPlantSpeciesDisplay.length > 0
                      ? AppContext.selectedPlantSpeciesDisplay : qsTr("No species set")
                color: Theme.colorTextVariant
            }
            Button {
                text: AppContext.selectedPlantSpecies.length > 0 ? qsTr("Change") : qsTr("Choose…")
                onClicked: speciesPicker.open()
            }
        }

        // ---- Sensors ----
        RowLayout {
            Layout.fillWidth: true
            SectionHeader {
                text: qsTr("Sensors")
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Pair sensor")
                onClicked: attachDialog.open()
            }
        }

        Label {
            visible: AppContext.boundSensors.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeLabel
            text: qsTr("No sensor paired. Pair one to track this plant's readings — history follows the plant even across sensor swaps.")
        }

        Repeater {
            model: AppContext.boundSensors
            delegate: Card {
                id: sensorRow
                required property var modelData
                Layout.fillWidth: true

                RowLayout {
                    spacing: Theme.spacingSm
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs / 2
                        Label {
                            text: sensorRow.modelData.model.length > 0
                                  ? sensorRow.modelData.model : qsTr("Sensor")
                            font.bold: true
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            // The hardware address disambiguates same-model sensors.
                            text: {
                                let tail = sensorRow.modelData.role.length > 0
                                    ? qsTr("since %1 · %2").arg(sensorRow.modelData.since)
                                                            .arg(sensorRow.modelData.role)
                                    : qsTr("since %1").arg(sensorRow.modelData.since);
                                return sensorRow.modelData.address.length > 0
                                    ? sensorRow.modelData.address + " · " + tail : tail;
                            }
                            color: Theme.colorTextVariant
                            font.pixelSize: Theme.fontSizeCaption
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Button {
                        flat: true
                        text: qsTr("Detach")
                        onClicked: AppContext.detachSensor(sensorRow.modelData.sensorId)
                    }
                }
            }
        }

        // ---- Care thresholds ----
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSm
            SectionHeader {
                text: qsTr("Care thresholds")
                Layout.fillWidth: true
            }
            Button {
                flat: true
                text: qsTr("Reset to species")
                enabled: AppContext.selectedPlantSpecies.length > 0
                onClicked: AppContext.resetCareThresholds()
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeLabel
            text: qsTr("The ideal range each reading is judged against. Leave a field blank for no limit on that side; pick a species to seed these automatically.")
        }

        Repeater {
            model: AppContext.careThresholds
            delegate: RowLayout {
                id: thr
                required property int quantity
                required property string label
                required property string unit
                required property string minText
                required property string maxText
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                function commit() {
                    AppContext.editCareThreshold(thr.quantity, minField.text, maxField.text);
                }

                Label {
                    text: thr.label
                    Layout.fillWidth: true
                    elide: Label.ElideRight
                }
                TextField {
                    id: minField
                    text: thr.minText
                    placeholderText: qsTr("min")
                    horizontalAlignment: TextInput.AlignRight
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    font.pixelSize: Theme.fontSizeBody
                    Layout.preferredWidth: 100
                    Layout.minimumWidth: 88
                    Layout.preferredHeight: implicitHeight
                    onEditingFinished: thr.commit()
                }
                Label { text: "–"; color: Theme.colorTextVariant }
                TextField {
                    id: maxField
                    text: thr.maxText
                    placeholderText: qsTr("max")
                    horizontalAlignment: TextInput.AlignRight
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    font.pixelSize: Theme.fontSizeBody
                    Layout.preferredWidth: 100
                    Layout.minimumWidth: 88
                    Layout.preferredHeight: implicitHeight
                    onEditingFinished: thr.commit()
                }
                Label {
                    text: thr.unit
                    color: Theme.colorTextVariant
                    elide: Label.ElideRight
                    // Fixed width so the unit column is constant across rows — a long
                    // unit (µS/cm) must not shift the inputs left relative to "%".
                    Layout.preferredWidth: 52
                    Layout.minimumWidth: 52
                    Layout.maximumWidth: 52
                }
            }
        }

        Item { Layout.fillHeight: true } // push the actions below to the bottom

        // ---- Duplicate ---- clone this plant (incl. its shared history) so a once-shared
        // pot becomes two tracked plants. Transformative (colorPrimary), not destructive.
        ListItem {
            Layout.fillWidth: true
            onClicked: duplicateDialog.open()
            contentItem: RowLayout {
                spacing: Theme.spacingSm
                Icon {
                    icon.name: "content_copy"
                    icon.color: Theme.colorPrimary
                    icon.size: Theme.fontSizeTitle
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs / 2
                    Label {
                        text: qsTr("Duplicate this plant")
                        color: Theme.colorPrimary
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("Track a second plant with the same history — e.g. after repotting a shared pot.")
                        color: Theme.colorTextVariant
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // ---- Danger zone ----
        SectionHeader {
            Layout.fillWidth: true
            text: qsTr("Danger zone")
        }
        // Destructive action, error-coloured to read as such (our flat style has no
        // destructive Button variant yet, so compose one).
        ListItem {
            Layout.fillWidth: true
            onClicked: confirmDelete.open()
            contentItem: RowLayout {
                spacing: Theme.spacingSm
                Icon {
                    icon.name: "delete"
                    icon.color: Theme.colorBad
                    icon.size: Theme.fontSizeTitle
                }
                Label {
                    text: qsTr("Delete this plant")
                    color: Theme.colorBad
                    font.bold: true
                    Layout.fillWidth: true
                }
            }
        }
    }

    // Pair a discovered sensor with this plant. Scans while open; tapping a device
    // binds it (minting/deduping the sensor from its handle) and closes.
    Dialog {
        id: attachDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.marginCompact, 460)
        height: Math.min(parent.height - 2 * Theme.marginCompact, 520)
        modal: true
        title: qsTr("Pair a sensor")
        standardButtons: Dialog.Close

        // Scanning is always-on (ADR 0011); startScan() is an idempotent no-op here, and the
        // dialog must NOT stopScan() on close or it would kill the app-wide background listener.
        onOpened: AppContext.startScan()

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingBase

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                BusyIndicator {
                    running: AppContext.scanning
                    implicitWidth: 24
                    implicitHeight: 24
                }
                Label {
                    text: AppContext.scanning ? qsTr("Scanning…") : qsTr("Scan stopped")
                    color: Theme.colorTextVariant
                    Layout.fillWidth: true
                }
            }

            ListView {
                id: pick
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: Theme.spacingXs
                model: AppContext.supportedDevices

                delegate: ListItem {
                    id: pickRow
                    width: ListView.view ? ListView.view.width : 0
                    required property string deviceId
                    required property string deviceName
                    required property string model
                    required property int valueCount
                    onClicked: {
                        AppContext.attachSensor(pickRow.deviceId);
                        attachDialog.close();
                    }

                    contentItem: ColumnLayout {
                        spacing: Theme.spacingXs / 2
                        Label {
                            text: pickRow.model.length > 0 ? pickRow.model
                                : (pickRow.deviceName.length > 0 ? pickRow.deviceName : qsTr("Sensor"))
                            font.bold: true
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("%1 · %2 values").arg(pickRow.deviceId).arg(pickRow.valueCount)
                            color: Theme.colorTextVariant
                            font.pixelSize: Theme.fontSizeCaption
                            elide: Label.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.spacingMd
                    visible: pick.count === 0
                    horizontalAlignment: Qt.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.colorTextVariant
                    text: qsTr("Scanning for supported sensors…")
                }
            }
        }
    }

    // Duplicate the selected plant, then carry out the optional sensor repot. The C++
    // duplicate switches the selection to the new plant, so the detach/attach calls below
    // (and this whole screen) now act on the duplicate; the original is left untouched.
    DuplicateDialog {
        id: duplicateDialog
        sourceName: AppContext.selectedPlantName
        hasSensor: AppContext.boundSensors.length > 0
        onConfirmed: (newName, sensorAction) => {
            const id = AppContext.duplicateSelectedPlant(newName);
            if (id.length === 0)
                return; // nothing selected / save failed (status already set)
            if (sensorAction !== 0) {
                // Detach the shared sensor(s) from the duplicate (its window stays closed
                // at "now", so it keeps the pre-split history).
                const sensors = AppContext.boundSensors;
                for (let i = 0; i < sensors.length; ++i)
                    AppContext.detachSensor(sensors[i].sensorId);
            }
            if (sensorAction === 2)
                attachDialog.open(); // reuse the pairing picker, now targeting the duplicate
        }
    }

    // Set/clear the selected plant's species from the catalog.
    SpeciesPickerDialog {
        id: speciesPicker
        currentKey: AppContext.selectedPlantSpecies
        onSpeciesChosen: (key) => AppContext.setSelectedPlantSpecies(key)
    }

    Dialog {
        id: confirmDelete
        anchors.centerIn: parent
        // Explicit width AND height (like attachDialog / SpeciesPickerDialog): a
        // content-sized Material Dialog wrapping a word-wrapped Label loops on
        // implicitHeight, because the Label's wrapped height (derived from its width)
        // feeds the Dialog's implicitHeight, which — with no fixed height — drives the
        // content width back. A fixed-size dialog with a fill-anchored layout breaks it.
        width: Math.min(parent.width - 2 * Theme.spacingMd, 360)
        height: Math.min(parent.height - 2 * Theme.spacingMd, 220)
        modal: true
        title: qsTr("Delete plant?")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            AppContext.removeSelectedPlant();
            // Return to the plant list (the deleted plant + its settings/detail are gone).
            NavigationController.goSection(NavigationController.Plants);
        }

        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("This permanently removes “%1”, its journal and sensor bindings. This cannot be undone.")
                      .arg(AppContext.selectedPlantName)
            }
        }
    }
}
