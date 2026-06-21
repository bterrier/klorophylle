// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import Klorophylle.Style
import Klorophylle

// Data export, backup & restore (ADR 0010), reached from the nav rail's "More".
// Two distinct jobs: a lossy readings CSV for spreadsheets, and a lossless JSON backup
// that can be restored. All logic is in C++ (AppContext + the pure persistence helpers);
// this screen only triggers invokables and shows the result.
Item {
    id: root
    property string title: qsTr("Export & backup")

    // Last action's outcome, shown inline beneath the cards.
    property string resultText: ""
    property bool resultOk: false
    property string lastFolderUrl: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        spacing: Theme.spacingMd

        // ---- Readings CSV (lossy, for analysis) ----
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: Theme.spacingXs
                Label {
                    text: qsTr("Export readings (CSV)")
                    font.pixelSize: Theme.fontSizeSubtitle
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.colorTextVariant
                    font.pixelSize: Theme.fontSizeLabel
                    text: qsTr("A spreadsheet of every reading in your chosen units. For analysis — it is not a backup and cannot be restored.")
                }
                Label {
                    text: qsTr("Period")
                    color: Theme.colorTextVariant
                    font.pixelSize: Theme.fontSizeLabel
                }
                ComboBox {
                    Layout.fillWidth: true
                    // The period table lives in C++ (AppContext); the chosen index
                    // persists via Settings.exportPeriodIndex and drives the CSV window.
                    model: AppContext.exportPeriodLabels()
                    currentIndex: Settings.exportPeriodIndex
                    onActivated: (index) => Settings.exportPeriodIndex = index
                }
                Button {
                    text: qsTr("Export readings")
                    onClicked: AppContext.exportReadingsCsv()
                }
            }
        }

        // ---- Full backup (lossless, restorable) ----
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: Theme.spacingXs
                Label {
                    text: qsTr("Back up all data")
                    font.pixelSize: Theme.fontSizeSubtitle
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.colorTextVariant
                    font.pixelSize: Theme.fontSizeLabel
                    text: qsTr("A complete, restorable snapshot of your plants, sensors, reading history, journal and care thresholds — for safekeeping or moving to another machine.")
                }
                Button {
                    text: qsTr("Back up now")
                    onClicked: AppContext.exportBackup()
                }
            }
        }

        // ---- Restore ----
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: Theme.spacingXs
                Label {
                    text: qsTr("Restore from backup")
                    font.pixelSize: Theme.fontSizeSubtitle
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.colorTextVariant
                    font.pixelSize: Theme.fontSizeLabel
                    text: qsTr("Load a backup file. Existing items are matched by identity and updated — nothing is duplicated.")
                }
                Button {
                    text: qsTr("Restore…")
                    onClicked: {
                        restoreDialog.currentFolder = AppContext.backupImportFolder();
                        restoreDialog.open();
                    }
                }
            }
        }

        // ---- Result line ----
        RowLayout {
            Layout.fillWidth: true
            visible: root.resultText.length > 0
            spacing: Theme.spacingSm
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: root.resultOk ? Theme.colorText : Theme.colorBad
                text: root.resultText
            }
            Button {
                visible: root.lastFolderUrl.length > 0
                text: qsTr("Show in folder")
                onClicked: AppContext.revealExportFolder()
            }
        }

        Item { Layout.fillHeight: true }
    }

    // Pick a backup JSON; the C++ side reads it and restores through the repositories.
    FileDialog {
        id: restoreDialog
        title: qsTr("Select a Klorophylle backup")
        nameFilters: [qsTr("Klorophylle backup (*.json)"), qsTr("All files (*)")]
        // currentFolder is set imperatively on open (see the button above).
        onAccepted: AppContext.restoreBackup(selectedFile)
    }

    Connections {
        target: AppContext
        function onExportFinished(summary, ok, folderUrl) {
            root.resultText = summary;
            root.resultOk = ok;
            root.lastFolderUrl = ok ? folderUrl : "";
        }
        function onRestoreFinished(summary, ok) {
            root.resultText = summary;
            root.resultOk = ok;
            root.lastFolderUrl = "";
        }
    }
}
