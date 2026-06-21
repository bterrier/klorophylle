// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// A reusable modal to associate a plant with one catalog species. Typeahead
// search drives the shared AppContext.catalogResults model; tapping a row (or "No
// species") emits speciesChosen with the catalog key (botanical name) or "" to clear.
// The caller decides what to do with it (add a plant vs. update the selected one).
Dialog {
    id: control

    property string currentKey: "" // highlight the plant's current species, if any

    signal speciesChosen(string key)

    anchors.centerIn: parent
    width: Math.min((parent ? parent.width : 360) - 2 * Theme.spacingMd, 420)
    height: Math.min((parent ? parent.height : 600) - 2 * Theme.spacingMd, 520)
    modal: true
    title: qsTr("Choose species")
    standardButtons: Dialog.Cancel

    onOpened: {
        searchField.clear();
        AppContext.searchCatalog("");
        searchField.forceActiveFocus();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingBase

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Search the plant catalog…")
            onTextChanged: AppContext.searchCatalog(text)
        }

        Button {
            text: qsTr("No species")
            flat: true
            Layout.fillWidth: true
            onClicked: {
                control.speciesChosen("");
                control.close();
            }
        }

        ListView {
            id: results
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingXs
            model: AppContext.catalogResults

            delegate: ListItem {
                id: row
                width: ListView.view ? ListView.view.width : 0
                required property string key
                required property string commonName
                highlighted: row.key === control.currentKey
                onClicked: {
                    control.speciesChosen(row.key);
                    control.close();
                }

                contentItem: ColumnLayout {
                    spacing: Theme.spacingXs / 2
                    Label {
                        text: row.key
                        font.bold: true
                        elide: Label.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: row.commonName.length > 0
                        text: row.commonName
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
                visible: results.count === 0
                horizontalAlignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                text: searchField.text.length > 0
                      ? qsTr("No matching species.")
                      : qsTr("Type to search the plant catalog.")
            }
        }
    }
}
