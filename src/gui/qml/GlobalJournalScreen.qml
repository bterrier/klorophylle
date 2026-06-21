// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollBar (no styled variant); styled controls below still win
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// The global (plant-less) journal (ADR 0022): the agent's user-wide memory entry plus any
// global notes the user adds. A full-page route reached from the AI assistant. Mirrors the per-plant
// journal tab — same delegate, same add/edit dialog — but scoped to AppContext.globalJournal and the
// global write verbs. Memory entries are agent-authored (cyan AI accent, uncreatable here); the only
// user-creatable kind is Note.
Item {
    id: root
    property string title: qsTr("Global memory")

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSm

        Label {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSm
            Layout.leftMargin: Theme.marginCompact
            Layout.rightMargin: Theme.marginCompact
            wrapMode: Text.WordWrap
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeLabel
            text: qsTr("Facts the assistant remembers across all your plants, plus your own global "
                       + "notes. Memory entries are written by the assistant — you can edit or "
                       + "delete them.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.marginCompact
            Layout.rightMargin: Theme.marginCompact
            Button {
                text: qsTr("Add note")
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
            model: AppContext.globalJournal
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: ListItem {
                id: entryRow
                width: ListView.view ? ListView.view.width : 0
                required property string entryId
                required property int kind
                required property string kindLabel
                required property string timestamp
                required property string editedAt
                required property string note
                required property string noteSummary
                required property bool isMemory

                // A git-commit-style row: kind + first line + date. Tap to read the full
                // entry rendered as markdown (where editing/deletion also live).
                onClicked: viewDialog.openForView(entryRow.entryId, entryRow.kind,
                                                  entryRow.kindLabel, entryRow.note,
                                                  entryRow.timestamp, entryRow.editedAt,
                                                  entryRow.isMemory)

                contentItem: RowLayout {
                    spacing: Theme.spacingSm
                    Label {
                        text: entryRow.kindLabel
                        font.bold: true
                        // Memory is agent-authored — mark it with the cyan AI accent.
                        color: entryRow.isMemory ? Theme.colorAI : Theme.colorText
                    }
                    Label {
                        text: entryRow.noteSummary
                        color: Theme.colorTextVariant
                        elide: Label.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: entryRow.timestamp
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
                text: qsTr("No global memory yet.\nThe assistant saves user-wide facts here, or tap "
                           + "“Add note” to jot one down.")
            }
        }
    }

    Dialog {
        id: entryDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spacingMd, 460)
        height: Math.min(implicitHeight, parent.height - 2 * Theme.spacingMd)
        modal: true
        title: editEntryId.length > 0 ? qsTr("Edit entry") : qsTr("Add note")
        standardButtons: Dialog.Ok | Dialog.Cancel

        // Empty == add mode; a non-empty entry id puts the dialog in edit mode.
        property string editEntryId: ""
        property int initialKind: 0
        property string initialKindLabel: ""
        property string initialNote: ""
        // A creatable kind (Note) picks from the ComboBox; an agent-authored kind (Memory) is fixed —
        // it sits outside the creatable list, so its kind is shown read-only and passed unchanged.
        readonly property bool kindEditable: initialKind < AppContext.globalCreatableJournalKinds.length

        function openForAdd() {
            editEntryId = "";
            initialKind = 0;
            initialKindLabel = "";
            initialNote = "";
            open();
        }
        function openForEdit(id, kind, kindLabel, note) {
            editEntryId = id;
            initialKind = kind;
            initialKindLabel = kindLabel;
            initialNote = note;
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
            if (editEntryId.length > 0)
                AppContext.editGlobalJournalEntry(editEntryId, k, noteField.text);
            else
                AppContext.addGlobalJournalEntry(k, noteField.text);
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingBase
            ComboBox {
                id: kindBox
                visible: entryDialog.kindEditable
                Layout.fillWidth: true
                model: AppContext.globalCreatableJournalKinds
            }
            Label {
                // Read-only kind for an agent-authored entry (Memory) — cyan AI accent, no picker.
                visible: !entryDialog.kindEditable
                text: entryDialog.initialKindLabel
                color: Theme.colorAI
                font.bold: true
            }
            // Scrollable multi-line editor — notes are markdown and may span many lines.
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 140
                Layout.preferredHeight: 240
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                TextArea {
                    id: noteField
                    placeholderText: qsTr("Note (optional)")
                }
            }
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

        function openForView(id, kind, kindLabel, note, timestamp, editedAt, isMemory) {
            viewEntryId = id;
            viewKind = kind;
            viewKindLabel = kindLabel;
            viewNote = note;
            viewTimestamp = timestamp;
            viewEditedAt = editedAt;
            viewIsMemory = isMemory;
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
                        AppContext.removeGlobalJournalEntry(viewDialog.viewEntryId);
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
        }
    }
}
