// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView (no styled variant); styled controls below still win
import QtQuick.Dialogs // FileDialog — pick a photo to attach to a journal entry
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// A single plant. Two tabs: CARE (current readings aggregated across its bound
// sensors, plus attach/detach + swap) and JOURNAL (entries added with no sensor
// involved). All labels/values/timestamps are formatted in C++ and surfaced as
// roles/properties; history follows the plant across sensor swaps (ADR 0005).
Item {
    id: root
    property string title: AppContext.selectedPlantName

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Species — associated with one catalog entry. Read-only here; changing it
        // lives in the plant settings subscreen (⚙), alongside the other editables.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.marginCompact
            Layout.bottomMargin: 0
            spacing: Theme.spacingSm
            Label {
                Layout.fillWidth: true
                elide: Label.ElideRight
                text: AppContext.selectedPlantSpeciesDisplay.length > 0
                      ? AppContext.selectedPlantSpeciesDisplay : qsTr("No species set")
                color: Theme.colorTextVariant
            }
            ToolButton {
                icon.name: "settings"
                onClicked: NavigationController.push(NavigationController.PlantSettings)
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Care") }
            TabButton { text: qsTr("Journal") }
            TabButton { text: qsTr("Sensors") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---- CARE ---- (current readings, aggregated across the plant's sensors;
            // sensor pairing lives in the plant settings subscreen.)
            ColumnLayout {
                spacing: Theme.spacingSm

                SectionHeader {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    Layout.leftMargin: Theme.marginCompact
                    Layout.rightMargin: Theme.marginCompact
                    text: qsTr("Current")
                    visible: AppContext.boundSensors.length > 0
                }

                ListView {
                    id: readings
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.marginCompact
                    Layout.rightMargin: Theme.marginCompact
                    clip: true
                    spacing: Theme.spacingXs
                    model: AppContext.careReadings

                    delegate: ListItem {
                        id: readingRow
                        width: ListView.view ? ListView.view.width : 0
                        required property int quantity
                        required property string label
                        required property string valueText
                        required property bool present
                        required property int status // CareStatus: 0 Unknown,1 Low,2 Ideal,3 High
                        required property real fraction // 0..1 position in the ideal range
                        required property bool hasRange // a both-bounded range exists
                        // Tap a reading to see its history charted (follows the plant).
                        onClicked: NavigationController.push(NavigationController.History,
                                                            { quantity: readingRow.quantity,
                                                              quantityLabel: readingRow.label })

                        contentItem: ColumnLayout {
                            spacing: Theme.spacingXs
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm
                                Label {
                                    text: readingRow.label
                                    Layout.fillWidth: true
                                }
                                // Care status: too-low/ideal/too-high, hidden when unjudged.
                                StatusPill {
                                    visible: readingRow.status > 0 && readingRow.present
                                    text: Format.careStatusLabel(readingRow.status)
                                    pillColor: Theme.careStatusColor(readingRow.status)
                                }
                                Label {
                                    text: readingRow.valueText
                                    font.bold: true
                                    color: (readingRow.status > 0 && readingRow.present)
                                           ? Theme.careStatusColor(readingRow.status) : Theme.colorText
                                    opacity: readingRow.present ? 1.0 : 0.4
                                }
                                Icon {
                                    icon.name: "chevron_right"
                                    icon.size: Theme.fontSizeTitle
                                    opacity: 0.4
                                }
                            }
                            // The value's place in the ideal range — green→cyan gradient bar,
                            // shown only when a range exists and a value is present.
                            ProgressBar {
                                visible: readingRow.hasRange && readingRow.present
                                Layout.fillWidth: true
                                value: readingRow.fraction
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.spacingMd
                        visible: readings.count === 0
                        horizontalAlignment: Qt.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.colorTextVariant
                        text: AppContext.boundSensors.length === 0
                              ? qsTr("No sensor attached.\nOpen settings (⚙) to pair one — history will follow this plant even if you swap sensors later.")
                              : qsTr("Waiting for values from the attached sensor(s)…")
                    }
                }
            }

            // ---- JOURNAL ----
            ColumnLayout {
                spacing: Theme.spacingSm

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    Layout.leftMargin: Theme.marginCompact
                    Layout.rightMargin: Theme.marginCompact
                    Button {
                        text: qsTr("Add entry")
                        onClicked: entryDialog.openForAdd()
                    }
                    Item { Layout.fillWidth: true }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.marginCompact
                    Layout.rightMargin: Theme.marginCompact
                    clip: true
                    spacing: Theme.spacingXs
                    model: AppContext.journal

                    delegate: ListItem {
                        id: row
                        width: ListView.view ? ListView.view.width : 0
                        required property string entryId
                        required property int kind
                        required property string kindLabel
                        required property string timestamp
                        required property string editedAt
                        required property string note
                        required property string noteSummary
                        required property bool isMemory
                        required property var attachments

                        // A git-commit-style row: kind + first line + date. Tap to read the full
                        // entry rendered as markdown (where editing/deletion + photos also live).
                        onClicked: viewDialog.openForView(row.entryId, row.kind, row.kindLabel,
                                                          row.note, row.timestamp, row.editedAt,
                                                          row.isMemory, row.attachments)

                        contentItem: RowLayout {
                            spacing: Theme.spacingSm
                            Label {
                                text: row.kindLabel
                                font.bold: true
                                // Memory is agent-authored — mark it with the cyan AI accent.
                                color: row.isMemory ? Theme.colorAI : Theme.colorText
                            }
                            Label {
                                text: row.noteSummary
                                color: Theme.colorTextVariant
                                elide: Label.ElideRight
                                Layout.fillWidth: true
                            }
                            // Photo-count chip — the entry carries at least one attachment.
                            Label {
                                visible: row.attachments.length > 0
                                text: "▣ " + row.attachments.length
                                color: Theme.colorTextVariant
                                font.pixelSize: Theme.fontSizeCaption
                            }
                            Label {
                                text: row.timestamp
                                color: Theme.colorTextVariant
                                font.pixelSize: Theme.fontSizeCaption
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
                        text: qsTr("No journal entries yet.\nTap “Add entry” to log watering, notes and more.")
                    }
                }
            }

            // ---- SENSORS ---- (the plant's bound sensors with live status; read-only —
            // pairing/detach live in plant settings. A row opens the sensor-detail page.)
            ColumnLayout {
                spacing: Theme.spacingSm

                ListView {
                    id: sensors
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: 12
                    Layout.leftMargin: Theme.marginCompact
                    Layout.rightMargin: Theme.marginCompact
                    clip: true
                    spacing: Theme.spacingXs
                    model: AppContext.sensorStatus

                    delegate: ListItem {
                        id: sensorRow
                        width: ListView.view ? ListView.view.width : 0
                        required property string sensorId
                        required property string model
                        required property string address
                        required property string since
                        required property string role
                        required property int liveness // 0 Offline, 1 Stale, 2 Live; <0 unknown
                        required property string battery
                        required property string lastSeen
                        required property bool gattOpen
                        // Open the same sensor-detail page as tapping a card in the Sensors view
                        // (history + status + guarded delete), keyed on the registered sensor id.
                        onClicked: {
                            AppContext.selectRegisteredSensor(sensorRow.sensorId);
                            NavigationController.push(NavigationController.SensorDetail);
                        }

                        contentItem: RowLayout {
                            spacing: Theme.spacingSm
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: Theme.spacingSm
                                implicitHeight: Theme.spacingSm
                                radius: width / 2
                                visible: sensorRow.liveness >= 0
                                color: Theme.livenessColor(sensorRow.liveness)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingXs / 2
                                Label {
                                    text: sensorRow.model.length > 0 ? sensorRow.model : qsTr("Sensor")
                                    font.bold: true
                                    elide: Label.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    // Address (disambiguates same-model sensors) + role, then a
                                    // status line: connected / battery / last-seen.
                                    text: {
                                        let head = sensorRow.address;
                                        if (sensorRow.role.length > 0)
                                            head = head.length > 0 ? head + " · " + sensorRow.role
                                                                   : sensorRow.role;
                                        let parts = [];
                                        if (sensorRow.gattOpen) parts.push(qsTr("connected"));
                                        if (sensorRow.battery.length > 0)
                                            parts.push(qsTr("battery %1").arg(sensorRow.battery));
                                        if (sensorRow.lastSeen.length > 0) parts.push(sensorRow.lastSeen);
                                        return parts.length > 0 ? head + " · " + parts.join(" · ") : head;
                                    }
                                    color: sensorRow.gattOpen ? Theme.colorAI : Theme.colorTextVariant
                                    font.pixelSize: Theme.fontSizeCaption
                                    elide: Label.ElideRight
                                    Layout.fillWidth: true
                                }
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
                        visible: sensors.count === 0
                        horizontalAlignment: Qt.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.colorTextVariant
                        text: qsTr("No sensor attached.\nOpen settings (⚙) to pair one.")
                    }
                }
            }
        }
    }

    Dialog {
        id: entryDialog
        anchors.centerIn: parent
        // Roomy: wide enough to write in, and as tall as the window allows so the editor
        // gets real space. The explicit footer below absorbs the rest, so Save/Cancel
        // always stay on-screen.
        width: Math.min(parent.width - 2 * Theme.spacingMd, 460)
        height: Math.min(implicitHeight, parent.height - 2 * Theme.spacingMd)
        modal: true
        title: editEntryId.length > 0 ? qsTr("Edit journal entry") : qsTr("Add journal entry")

        // Explicit Save/Cancel footer — a plain laid-out bar (not standardButtons) with a
        // guaranteed height, so the buttons are always visible regardless of content size.
        footer: Item {
            implicitHeight: footerButtons.implicitHeight + 2 * Theme.spacingMd
            RowLayout {
                id: footerButtons
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: Theme.spacingMd
                spacing: Theme.spacingSm
                Button {
                    text: qsTr("Cancel")
                    onClicked: entryDialog.reject()
                }
                Button {
                    text: qsTr("Save")
                    onClicked: entryDialog.accept()
                }
            }
        }

        // Empty == add mode; a non-empty entry id puts the dialog in edit mode.
        property string editEntryId: ""
        property int initialKind: 0
        property string initialKindLabel: ""
        property string initialNote: ""
        // A creatable kind picks from the ComboBox; an agent-authored kind (Memory) is fixed —
        // it sits outside the creatable list, so its kind is shown read-only and passed unchanged.
        readonly property bool kindEditable: initialKind < AppContext.creatableJournalKinds.length

        // Photo staging: all photo changes are committed on Save, so Cancel discards them.
        //  · existingPhotos — already-attached photos ({attachmentId, url}), loaded in edit mode.
        //  · pendingRemoves — attachmentIds of existing photos the user tapped to delete.
        //  · stagedAdds     — file:// URLs picked but not yet attached (no entry id exists in add mode).
        property var existingPhotos: []
        property var pendingRemoves: []
        property var stagedAdds: []

        function openForAdd() {
            editEntryId = "";
            initialKind = 0;
            initialKindLabel = "";
            initialNote = "";
            existingPhotos = [];
            pendingRemoves = [];
            stagedAdds = [];
            open();
        }
        function openForEdit(id, kind, kindLabel, note) {
            editEntryId = id;
            initialKind = kind;
            initialKindLabel = kindLabel;
            initialNote = note;
            existingPhotos = AppContext.photosForEntry(id);
            pendingRemoves = [];
            stagedAdds = [];
            open();
        }

        onOpened: {
            if (kindEditable)
                kindBox.currentIndex = initialKind;
            noteField.text = initialNote;
            noteField.forceActiveFocus();
        }
        onAccepted: {
            var k = kindEditable ? kindBox.currentIndex : initialKind;
            AppContext.saveJournalEntry(editEntryId, k, noteField.text,
                                        stagedAdds, pendingRemoves);
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingBase
            ComboBox {
                id: kindBox
                visible: entryDialog.kindEditable
                Layout.fillWidth: true
                model: AppContext.creatableJournalKinds
            }
            Label {
                // Read-only kind for an agent-authored entry (Memory) — cyan AI accent, no picker.
                visible: !entryDialog.kindEditable
                text: entryDialog.initialKindLabel
                color: Theme.colorAI
                font.bold: true
            }
            // Scrollable multi-line editor (Qt docs' recommended TextArea-in-ScrollView
            // pattern): ScrollView sizes the content, keeps the field background fixed and
            // clips. A full note — e.g. a report pasted from a previous gardening app — is
            // editable as a block, not one scrolling line.
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // A generous default writing area (not tied to current content), growing to
                // fill a taller window; scrolls internally once the note exceeds the space.
                Layout.minimumHeight: 140
                Layout.preferredHeight: 240
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                TextArea {
                    id: noteField
                    placeholderText: qsTr("Note (optional)")
                }
            }

            // Photos: existing photos (not pending-removal) plus staged additions, as a
            // thumbnail strip; tap a thumbnail to remove/unstage it. All changes commit on Save.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                visible: entryDialog.existingPhotos.length > entryDialog.pendingRemoves.length
                         || entryDialog.stagedAdds.length > 0
                Repeater {
                    model: entryDialog.existingPhotos
                    delegate: Item {
                        required property var modelData
                        visible: entryDialog.pendingRemoves.indexOf(modelData.attachmentId) < 0
                        width: visible ? 72 : 0
                        height: visible ? 72 : 0
                        Image {
                            anchors.fill: parent
                            source: parent.modelData.url
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            // A restored-without-files backup leaves a missing file — show nothing
                            // rather than a broken-image glyph (ADR 0024 decision 7).
                            Rectangle {
                                anchors.fill: parent
                                visible: parent.status !== Image.Ready
                                color: Theme.colorForegroundElevated
                                border.color: Theme.colorSeparator
                            }
                        }
                        // Tap to mark this already-attached photo for removal on Save.
                        TapHandler {
                            onTapped: entryDialog.pendingRemoves =
                                entryDialog.pendingRemoves.concat([parent.modelData.attachmentId])
                        }
                    }
                }
                Repeater {
                    model: entryDialog.stagedAdds
                    delegate: Item {
                        required property int index
                        required property var modelData
                        width: 72
                        height: 72
                        Image {
                            anchors.fill: parent
                            source: parent.modelData // the picked file:// URL, shown before attach
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        // Tap to drop a staged (not-yet-attached) photo.
                        TapHandler {
                            onTapped: {
                                var a = entryDialog.stagedAdds.slice();
                                a.splice(parent.index, 1);
                                entryDialog.stagedAdds = a;
                            }
                        }
                    }
                }
            }
            Button {
                text: qsTr("Add photo")
                onClicked: photoPicker.open()
            }
        }

        FileDialog {
            id: photoPicker
            title: qsTr("Choose a photo")
            nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.gif)"), qsTr("All files (*)")]
            onAccepted: entryDialog.stagedAdds =
                entryDialog.stagedAdds.concat([selectedFile.toString()])
        }
    }

    // Read-only view of one entry: the full note rendered as markdown. Editing and deletion
    // are reached from here (the list rows are just tappable subject lines). "Edit" hands off
    // to entryDialog with the raw text — the user always edits/types plain text.
    Dialog {
        id: viewDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spacingMd, 460)
        height: Math.min(implicitHeight, parent.height - 2 * Theme.spacingMd)
        modal: true
        title: viewKindLabel

        property string viewEntryId: ""
        property int viewKind: 0
        property string viewKindLabel: ""
        property string viewNote: ""
        property string viewTimestamp: ""
        property string viewEditedAt: ""
        property bool viewIsMemory: false
        // The entry's photos ({attachmentId, url, caption}); refreshed after an add/remove so the
        // thumbnail strip stays live without re-reading the list model (AppContext.photosForEntry).
        property var viewAttachments: []

        function openForView(id, kind, kindLabel, note, timestamp, editedAt, isMemory, attachments) {
            viewEntryId = id;
            viewKind = kind;
            viewKindLabel = kindLabel;
            viewNote = note;
            viewTimestamp = timestamp;
            viewEditedAt = editedAt;
            viewIsMemory = isMemory;
            viewAttachments = attachments;
            open();
        }

        footer: Item {
            implicitHeight: viewFooterButtons.implicitHeight + 2 * Theme.spacingMd
            RowLayout {
                id: viewFooterButtons
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                spacing: Theme.spacingSm
                Button {
                    text: qsTr("Delete")
                    onClicked: {
                        AppContext.removeJournalEntry(viewDialog.viewEntryId);
                        viewDialog.close();
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Close")
                    onClicked: viewDialog.close()
                }
                Button {
                    text: qsTr("Edit")
                    onClicked: {
                        viewDialog.close();
                        entryDialog.openForEdit(viewDialog.viewEntryId, viewDialog.viewKind,
                                                viewDialog.viewKindLabel, viewDialog.viewNote);
                    }
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingBase
            Label {
                text: viewDialog.viewTimestamp
                color: viewDialog.viewIsMemory ? Theme.colorAI : Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
            }
            Label {
                visible: viewDialog.viewEditedAt.length > 0
                text: qsTr("edited %1").arg(viewDialog.viewEditedAt)
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                font.italic: true
            }
            ScrollView {
                id: viewScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 140
                Layout.preferredHeight: 240
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                MarkdownLabel {
                    width: viewScroll.availableWidth
                    text: viewDialog.viewNote
                }
            }

            // Photos: a read-only thumbnail strip. Adding/removing photos lives in the edit
            // dialog (reached via "Edit"); this view just displays them.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                visible: viewDialog.viewAttachments.length > 0
                Repeater {
                    model: viewDialog.viewAttachments
                    delegate: Item {
                        required property var modelData
                        width: 72
                        height: 72
                        Image {
                            anchors.fill: parent
                            source: parent.modelData.url
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            // A restored-without-files backup leaves a missing file — show nothing
                            // rather than a broken-image glyph (ADR 0024 decision 7).
                            Rectangle {
                                anchors.fill: parent
                                visible: parent.status !== Image.Ready
                                color: Theme.colorForegroundElevated
                                border.color: Theme.colorSeparator
                            }
                        }
                    }
                }
            }
        }
    }

}
