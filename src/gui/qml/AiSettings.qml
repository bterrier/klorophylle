// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView/ScrollBar (no styled variant); styled controls below still win
import QtQuick.Layouts
import Klorophylle.Style
import Klorophylle

// The AI chat. Non-secret config (endpoint, model) lives in Settings; the API key goes
// through the secret store, never QSettings (ADR 0019). Defaults target a local Ollama,
// which needs no key.
//
// The fields adapt to the selected provider via its onboarding descriptor (ADR 0027): the
// cloud providers have a fixed endpoint (the Endpoint field disappears) and need a key (with a
// "get a key" link); the model field suggests per-provider models but always accepts free text.
Item {
    id: root

    // The descriptor for the selected provider — re-read when the provider changes.
    readonly property var prov: AppContext.agent.providerDescriptor(Settings.agentProviderType)

    // One-tap onboarding: select a provider, seed its default model, enable the assistant. The
    // SettingsStore default endpoint for the local (BYO) branch.
    readonly property string localEndpoint: "http://localhost:11434/v1"

    function applyPreset(type) {
        let d = AppContext.agent.providerDescriptor(type);
        Settings.agentProviderType = type;
        Settings.agentModel = d.defaultModel;
        if (d.fixedEndpoint === "")
            Settings.agentBaseUrl = root.localEndpoint;
        Settings.agentEnabled = true;
    }

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

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Label {
                    text: qsTr("Enable assistant")
                    Layout.fillWidth: true
                }
                Switch {
                    checked: Settings.agentEnabled
                    onToggled: Settings.agentEnabled = checked
                }
            }

            // Quick setup — the two featured paths. Gemini (free, hosted) is the hero; Ollama is
            // the local/private option. Both seed the right provider + default model; the rest of
            // the screen stays the advanced surface for everything else.
            Label {
                Layout.fillWidth: true
                text: qsTr("Quick setup")
                font.bold: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Button {
                    highlighted: true
                    text: qsTr("Set up free Google Gemini")
                    onClicked: {
                        root.applyPreset(3); // Gemini
                        if (root.prov.keyUrl !== "")
                            Qt.openUrlExternally(root.prov.keyUrl);
                        apiKeyField.forceActiveFocus();
                    }
                }
                Button {
                    text: qsTr("Use local Ollama (private)")
                    onClicked: root.applyPreset(0) // OpenAI-compatible @ localhost
                }
                Button {
                    text: qsTr("Guided setup…")
                    onClicked: setupWizard.open()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
                Label {
                    text: qsTr("Provider type")
                    Layout.fillWidth: true
                }
                ComboBox {
                    // Provider-factory branch order: 0 = OpenAI-compatible (Chat Completions, covers
                    // Ollama / llama.cpp / vLLM / OpenRouter / BYOK), 1 = OpenAI Responses,
                    // 2 = Anthropic Messages, 3 = Gemini (ADR 0019 decision 3).
                    Layout.preferredWidth: 260
                    model: [qsTr("OpenAI-compatible"), qsTr("OpenAI Responses"), qsTr("Anthropic"),
                            qsTr("Gemini")]
                    currentIndex: Settings.agentProviderType
                    onActivated: (index) => Settings.agentProviderType = index
                }
            }

            // Only the OpenAI-compatible branch is a BYO endpoint; the cloud providers have a
            // fixed endpoint baked into the descriptor, so the field disappears for them.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
                visible: root.prov.fixedEndpoint === ""
                Label {
                    text: qsTr("Endpoint")
                    Layout.fillWidth: true
                }
                TextField {
                    Layout.preferredWidth: 260
                    text: Settings.agentBaseUrl
                    placeholderText: qsTr("http://localhost:11434/v1")
                    onEditingFinished: Settings.agentBaseUrl = text
                }
            }

            // An editable combo seeded from the provider's known models — suggestions, never a
            // cage: free text is always accepted, so a stale list never blocks the user.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
                Label {
                    text: qsTr("Model")
                    Layout.fillWidth: true
                }
                ComboBox {
                    id: modelBox
                    Layout.preferredWidth: 260
                    editable: true
                    model: root.prov.knownModels
                    onAccepted: Settings.agentModel = editText
                    onActivated: Settings.agentModel = currentText
                    // An editable combo resets editText to its first item whenever the model list
                    // is (re)assigned — on load and on every provider switch — so a one-shot
                    // initializer gets clobbered. Re-assert the persisted model after each such
                    // reset: select it when known, and set editText explicitly so a custom model
                    // not in the list still shows (free text, never a cage). Setting editText LAST
                    // wins over the currentIndex side-effect; skipped while the user is editing.
                    function showSavedModel() {
                        if (activeFocus)
                            return;
                        currentIndex = root.prov.knownModels.indexOf(Settings.agentModel);
                        editText = Settings.agentModel;
                    }
                    Component.onCompleted: showSavedModel()
                    onModelChanged: showSavedModel()
                    Connections {
                        // Reflect a model set elsewhere (a preset/wizard) without clobbering typing.
                        target: Settings
                        function onAgentChanged() { modelBox.showSavedModel(); }
                    }
                }
            }

            // Conservative capability warning: only fires when photo-sending is on AND the chosen
            // model is a KNOWN text-only one (e.g. stock Ollama qwen2.5). Free-text / unknown
            // models never warn, so the hint has no false positives.
            Label {
                Layout.fillWidth: true
                visible: Settings.agentEnabled && Settings.agentToolsEnabled
                         && Settings.agentVisionEnabled
                         && root.prov.textOnlyModels.indexOf(Settings.agentModel) !== -1
                wrapMode: Text.WordWrap
                color: Theme.colorWarn
                font.pixelSize: Theme.fontSizeCaption
                text: qsTr("Vision is on, but “%1” is text-only — photos won't be sent. Pick a "
                           + "vision-capable model or turn off “Send journal photos”.")
                          .arg(Settings.agentModel)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
                Label {
                    text: qsTr("Use tools")
                    Layout.fillWidth: true
                }
                Switch {
                    checked: Settings.agentToolsEnabled
                    onToggled: Settings.agentToolsEnabled = checked
                }
            }

            Label {
                Layout.fillWidth: true
                visible: Settings.agentEnabled
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                text: qsTr("Lets the assistant look up your plants and log care. Turn off for small "
                           + "local models that get overwhelmed by tool definitions.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled && Settings.agentToolsEnabled
                Label {
                    text: qsTr("Look up plants online")
                    Layout.fillWidth: true
                }
                Switch {
                    checked: Settings.agentWebToolEnabled
                    onToggled: Settings.agentWebToolEnabled = checked
                }
            }

            Label {
                Layout.fillWidth: true
                visible: Settings.agentEnabled && Settings.agentToolsEnabled
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                text: qsTr("Off by default. Lets the assistant fetch plant species pages from Wikipedia "
                           + "and Wikispecies — this sends your query to those sites, even with a local "
                           + "model.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled && Settings.agentToolsEnabled
                Label {
                    text: qsTr("Send journal photos")
                    Layout.fillWidth: true
                }
                Switch {
                    checked: Settings.agentVisionEnabled
                    onToggled: Settings.agentVisionEnabled = checked
                }
            }

            Label {
                Layout.fillWidth: true
                visible: Settings.agentEnabled && Settings.agentToolsEnabled
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                text: qsTr("Off by default. Lets the assistant look at a plant's journal photos to "
                           + "diagnose problems — only works with a vision-capable model, and sends "
                           + "those images to your configured provider.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
                Label {
                    text: qsTr("Reasoning effort")
                    Layout.fillWidth: true
                }
                ComboBox {
                    // klr::ReasoningEffort order: 0 = Off, 1 = Low, 2 = Medium, 3 = High. Reasoning
                    // models spend more thinking at higher effort; many models ignore it.
                    model: [qsTr("Off"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                    currentIndex: Settings.agentReasoningEffort
                    onActivated: (index) => Settings.agentReasoningEffort = index
                }
            }

            // The key field shows for the cloud providers (required, with a link) and for the
            // OpenAI-compatible branch (optional — local servers need none).
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
                visible: root.prov.needsKey || root.prov.fixedEndpoint === ""
                Label {
                    text: qsTr("API key")
                    Layout.fillWidth: true
                }
                TextField {
                    id: apiKeyField
                    Layout.preferredWidth: 260
                    echoMode: TextInput.Password
                    placeholderText: AppContext.agent.hasApiKey
                        ? qsTr("•••••• (set)")
                        : (root.prov.needsKey ? qsTr("paste your API key")
                                              : qsTr("leave blank for local"))
                    onEditingFinished: {
                        if (text.length > 0) {
                            AppContext.agent.setApiKey(text);
                            clear();
                        }
                    }
                }
            }

            // Per-provider "get a key" link — only when the provider has one (the cloud providers).
            Button {
                visible: Settings.agentEnabled && root.prov.keyUrl !== ""
                flat: true
                text: qsTr("Get an API key")
                icon.name: "open_in_new"
                onClicked: Qt.openUrlExternally(root.prov.keyUrl)
            }

            // Free-tier note: link out to the provider's own limits page rather than baking in
            // numbers that shift (the featured Gemini path has one).
            Button {
                visible: Settings.agentEnabled && root.prov.freeTierUrl !== ""
                flat: true
                text: qsTr("Free-tier limits")
                icon.name: "open_in_new"
                onClicked: Qt.openUrlExternally(root.prov.freeTierUrl)
            }

            // Foreshadow the data egress the chat itself discloses: a cloud provider receives your
            // messages, plant context and any sent photos.
            Label {
                Layout.fillWidth: true
                visible: Settings.agentEnabled && AppContext.agent.endpointIsRemote
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                text: qsTr("This is a cloud provider: your messages, plant details and any photos "
                           + "you send leave this device to %1.").arg(root.prov.displayName)
            }

            Label {
                Layout.fillWidth: true
                visible: Settings.agentEnabled
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                text: qsTr("Local endpoints (Ollama, llama.cpp) need no key. Any key is stored in your "
                           + "system keyring (Secret Service); without a keyring it is kept only until you quit.")
            }
        }
    }

    // The guided alternative to the fields above; launched from "Guided setup…".
    AiSetupWizard { id: setupWizard }
}
