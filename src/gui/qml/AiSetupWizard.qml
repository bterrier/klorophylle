// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ButtonGroup / StackLayout pages — not in Klorophylle.Style
import QtQuick.Layouts
import Klorophylle.Style
import Klorophylle

// A short guided alternative to the adaptive AI settings fields (ADR 0027): pick a provider →
// paste a key (only when the provider needs one) → confirm. It writes the same Settings + key
// seam the inline fields use — no backend of its own. The descriptor drives every per-provider
// detail (which providers need a key, the key page, the default model, the fixed endpoint).
Dialog {
    id: root

    // Wizard state: step 0 = provider, 1 = key, 2 = confirm; the provider-type index chosen.
    property int step: 0
    property int selectedType: 3 // Gemini — the featured free default

    readonly property var sel: AppContext.agent.providerDescriptor(root.selectedType)
    // The SettingsStore default endpoint for the local (BYO) branch.
    readonly property string localEndpoint: "http://localhost:11434/v1"

    anchors.centerIn: parent
    width: Math.min(parent.width - 2 * Theme.spacingMd, 460)
    modal: true
    title: qsTr("Set up the AI assistant")
    standardButtons: Dialog.NoButton

    // Fresh state on each open (the instance is reused).
    onAboutToShow: {
        root.step = 0;
        root.selectedType = 3;
        keyField.clear();
        geminiChoice.checked = true;
    }

    function finish() {
        Settings.agentProviderType = root.selectedType;
        Settings.agentModel = root.sel.defaultModel;
        if (root.sel.fixedEndpoint === "")
            Settings.agentBaseUrl = root.localEndpoint;
        if (root.sel.needsKey && keyField.text.length > 0)
            AppContext.agent.setApiKey(keyField.text);
        Settings.agentEnabled = true;
        root.close();
    }

    ButtonGroup { id: providerGroup }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        StackLayout {
            Layout.fillWidth: true
            currentIndex: root.step

            // Step 0 — choose a provider.
            ColumnLayout {
                spacing: Theme.spacingSm
                Label { text: qsTr("Choose a provider"); font.bold: true }
                RadioButton {
                    id: geminiChoice
                    checked: true
                    ButtonGroup.group: providerGroup
                    text: qsTr("Google Gemini — free, hosted")
                    onClicked: root.selectedType = 3
                }
                RadioButton {
                    ButtonGroup.group: providerGroup
                    text: qsTr("Local Ollama — private, on this device")
                    onClicked: root.selectedType = 0
                }
                RadioButton {
                    ButtonGroup.group: providerGroup
                    text: qsTr("OpenAI")
                    onClicked: root.selectedType = 1
                }
                RadioButton {
                    ButtonGroup.group: providerGroup
                    text: qsTr("Anthropic")
                    onClicked: root.selectedType = 2
                }
            }

            // Step 1 — paste a key (only reached when the provider needs one).
            ColumnLayout {
                spacing: Theme.spacingSm
                Label { text: qsTr("Add your API key"); font.bold: true }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.colorTextVariant
                    font.pixelSize: Theme.fontSizeCaption
                    text: qsTr("%1 needs a key. Open its page, create a key, and paste it below. "
                               + "It is stored in your system keyring, never in plain settings.")
                              .arg(root.sel.displayName)
                }
                Button {
                    visible: root.sel.keyUrl !== ""
                    flat: true
                    text: qsTr("Get an API key")
                    icon.name: "open_in_new"
                    onClicked: Qt.openUrlExternally(root.sel.keyUrl)
                }
                TextField {
                    id: keyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: qsTr("paste your API key")
                }
            }

            // Step 2 — confirm.
            ColumnLayout {
                spacing: Theme.spacingSm
                Label { text: qsTr("Ready"); font.bold: true }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeBody
                    text: qsTr("Provider: %1\nModel: %2").arg(root.sel.displayName)
                                                         .arg(root.sel.defaultModel)
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: root.sel.fixedEndpoint !== ""
                    color: Theme.colorTextVariant
                    font.pixelSize: Theme.fontSizeCaption
                    text: qsTr("This is a cloud provider: your messages and plant context will leave "
                               + "this device. You can change the model anytime in AI settings.")
                }
            }
        }

        // Navigation. Key step is skipped for keyless providers (local Ollama).
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Button {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Back")
                visible: root.step > 0
                onClicked: root.step = (root.step === 2 && !root.sel.needsKey) ? 0 : root.step - 1
            }
            Button {
                text: root.step === 2 ? qsTr("Finish") : qsTr("Next")
                highlighted: true
                onClicked: {
                    if (root.step === 2)
                        root.finish();
                    else if (root.step === 0)
                        root.step = root.sel.needsKey ? 1 : 2;
                    else
                        root.step = 2;
                }
            }
        }
    }
}
