// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView/ScrollBar (no styled variant); styled controls below still win
import QtQuick.Layouts
import QtQuick.Dialogs
import Klorophylle.Style
import Klorophylle

// Bring an existing WatchFlower database forward. The file is read untouched; the C++ side
// reports back through AppContext.importFinished.
Item {
    id: root

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

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeLabel
                text: qsTr("Bring an existing WatchFlower database forward. The file is read "
                           + "untouched; its plants, sensors and history are added here.")
            }
            // One-click import of the auto-detected database (common case). A native file
            // dialog can't be forced to a start folder — the portal remembers its own last
            // location — so when the standard data.db exists, skip the dialog entirely.
            ColumnLayout {
                Layout.fillWidth: true
                visible: AppContext.detectedLegacyDatabase().length > 0
                spacing: Theme.spacingXs
                Button {
                    text: qsTr("Import detected WatchFlower data")
                    onClicked: AppContext.importLegacyDatabase(AppContext.detectedLegacyDatabase())
                }
                Label {
                    Layout.fillWidth: true
                    elide: Label.ElideMiddle
                    color: Theme.colorTextVariant
                    font.pixelSize: Theme.fontSizeCaption
                    text: AppContext.detectedLegacyDatabase().replace("file://", "")
                }
            }

            // Fallback for a database in a non-standard location (opens the native picker; its
            // start folder is up to the OS dialog).
            Button {
                text: qsTr("Choose a database file…")
                onClicked: {
                    importDialog.currentFolder = AppContext.legacyImportFolder();
                    importDialog.open();
                }
            }
        }
    }

    // Pick a WatchFlower data.db; the C++ side reads it read-only and reports back.
    FileDialog {
        id: importDialog
        title: qsTr("Select a WatchFlower database")
        nameFilters: [qsTr("WatchFlower database (*.db)"), qsTr("All files (*)")]
        // currentFolder is set imperatively on open (see the button above).
        onAccepted: AppContext.importLegacyDatabase(selectedFile)
    }

    Connections {
        target: AppContext
        function onImportFinished(summary, ok) {
            importResult.text = summary;
            importResult.open();
        }
    }

    Dialog {
        id: importResult
        property alias text: resultLabel.text
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spacingMd, 360)
        modal: true
        title: qsTr("Import")
        standardButtons: Dialog.Ok
        Label {
            id: resultLabel
            width: parent.width
            wrapMode: Text.WordWrap
        }
    }
}
