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

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
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

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
                Label {
                    text: qsTr("Model")
                    Layout.fillWidth: true
                }
                TextField {
                    Layout.preferredWidth: 260
                    text: Settings.agentModel
                    placeholderText: qsTr("qwen2.5")
                    onEditingFinished: Settings.agentModel = text
                }
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

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                enabled: Settings.agentEnabled
                Label {
                    text: qsTr("API key")
                    Layout.fillWidth: true
                }
                TextField {
                    id: apiKeyField
                    Layout.preferredWidth: 260
                    echoMode: TextInput.Password
                    placeholderText: AppContext.agent.hasApiKey ? qsTr("•••••• (set)")
                                                                : qsTr("leave blank for local")
                    onEditingFinished: {
                        if (text.length > 0) {
                            AppContext.agent.setApiKey(text);
                            clear();
                        }
                    }
                }
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
}
