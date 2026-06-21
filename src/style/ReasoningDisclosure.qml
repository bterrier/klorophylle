// SPDX-License-Identifier: GPL-3.0-or-later
// A collapsed-by-default disclosure for model "reasoning" (thinking) text, carrying the cyan
// AI accent (ADR 0013 #5). The AI chat uses it for both committed reasoning rows and the live
// streaming preview (ADR 0019 decision 6). Tap the header to expand/collapse.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property alias text: body.text
    property bool expanded: false

    spacing: Theme.spacingXs

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingXs
        Icon {
            icon.name: root.expanded ? "expand_more" : "chevron_right"
            icon.color: Theme.colorAI
            icon.size: Theme.fontSizeLabel
            Layout.alignment: Qt.AlignVCenter
        }
        Label {
            text: qsTr("Reasoning")
            color: Theme.colorAI
            font.pixelSize: Theme.fontSizeCaption
            font.bold: true
        }
        Item { Layout.fillWidth: true }
        TapHandler { onTapped: root.expanded = !root.expanded }
    }

    Label {
        id: body
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacingMd
        visible: root.expanded && text.length > 0
        wrapMode: Text.WordWrap
        color: Theme.colorTextVariant
        font.pixelSize: Theme.fontSizeCaption
    }
}
