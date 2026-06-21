// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView (no styled variant); styled controls below still win
import QtQuick.Layouts
import Klorophylle.Style
import Klorophylle

// The AI assistant chat (ADR 0019 decision 7): a transcript ListView over
// AppContext.agent (an AgentViewModel) plus an input row. The agent's logic — the loop,
// tools, persistence — all lives in C++; this screen only renders rows and forwards input.
// Cyan (Theme.colorAI, via PulsingNode) is the AI accent (ADR 0013 #5). Reasoning ("thinking")
// is shown in a collapsed-by-default disclosure, both live and in reloaded transcripts.
Item {
    id: root
    property string title: qsTr("AI insights")

    // AgentViewModel.RowKind, mirrored as plain ints (the model is not a QML-registered type).
    readonly property int kindUser: 0
    readonly property int kindAssistant: 1
    readonly property int kindToolCall: 2
    readonly property int kindToolResult: 3
    readonly property int kindError: 4
    readonly property int kindReasoning: 5

    readonly property var agent: AppContext.agent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        spacing: Theme.spacingSm

        // Header action: start a fresh conversation (clears the transcript view; the agent
        // mints a new conversation and resets the session). No-ops while a turn is in flight.
        RowLayout {
            Layout.fillWidth: true
            // Open the global journal — the agent's user-wide memory + global notes (ADR 0022).
            Button {
                text: qsTr("Global memory")
                icon.name: "menu_book"
                icon.color: Theme.colorAI
                flat: true
                onClicked: NavigationController.push(NavigationController.GlobalJournal)
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("New conversation")
                icon.name: "add_comment"
                flat: true
                enabled: !root.agent.busy
                onClicked: root.agent.startNewConversation()
            }
        }

        // Not-configured hint (agent disabled or no model set in Settings).
        Label {
            Layout.fillWidth: true
            visible: !root.agent.ready
            wrapMode: Text.WordWrap
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeLabel
            text: qsTr("The AI assistant is turned off, or no model is set. Configure an endpoint "
                       + "and model in Settings → AI assistant.")
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSm
            model: root.agent
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            onCountChanged: Qt.callLater(positionViewAtEnd)

            delegate: Item {
                id: row
                required property int kind
                required property string text
                required property string toolName
                required property var images // data: URLs for a tool-result row's photos
                width: ListView.view.width
                // Exactly one of the two sub-views is visible per row (tool caption vs chat
                // bubble); size the delegate to whichever it is, or a tall answer overflows
                // its slot and the next row lays out on top of it.
                readonly property bool isToolRow: row.kind === root.kindToolCall
                                                  || row.kind === root.kindToolResult
                readonly property bool isReasoningRow: row.kind === root.kindReasoning
                implicitHeight: isReasoningRow ? reasoning.implicitHeight
                              : isToolRow ? content.implicitHeight
                              : bubble.implicitHeight

                // Tool activity: a compact caption line (not a bubble), plus any photos the tool
                // returned as a thumbnail strip (what a vision model saw / a remote got).
                ColumnLayout {
                    id: content
                    width: parent.width
                    visible: row.kind === root.kindToolCall || row.kind === root.kindToolResult
                    spacing: Theme.spacingXs
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        Icon {
                            icon.name: "settings"
                            icon.color: Theme.colorAI
                            icon.size: Theme.fontSizeLabel
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.colorTextVariant
                            font.pixelSize: Theme.fontSizeCaption
                            text: row.kind === root.kindToolCall
                                  ? qsTr("Looking up %1…").arg(row.toolName)
                                  : row.text
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        visible: row.images.length > 0
                        Repeater {
                            model: row.images
                            delegate: Image {
                                required property string modelData
                                source: modelData
                                sourceSize.width: 96
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }
                        }
                    }
                }

                // User / assistant / error: a chat bubble, aligned by author.
                Row {
                    id: bubble
                    width: parent.width
                    visible: row.kind === root.kindUser || row.kind === root.kindAssistant
                             || row.kind === root.kindError
                    layoutDirection: row.kind === root.kindUser ? Qt.RightToLeft : Qt.LeftToRight
                    Rectangle {
                        width: Math.min(implicitWidth, parent.width * 0.85)
                        implicitWidth: bubbleText.implicitWidth + 2 * Theme.spacingSm
                        implicitHeight: bubbleText.implicitHeight + 2 * Theme.spacingSm
                        radius: Theme.radius
                        color: row.kind === root.kindUser ? Theme.colorPrimary : Theme.colorCard
                        border.width: row.kind === root.kindAssistant ? 1 : 0
                        border.color: Theme.colorAI
                        // Markdown-rendered (read-only); the user still types plain text in the
                        // input field below — only this committed view renders the formatting.
                        MarkdownLabel {
                            id: bubbleText
                            x: Theme.spacingSm
                            y: Theme.spacingSm
                            width: Math.min(implicitWidth, row.width * 0.85 - 2 * Theme.spacingSm)
                            text: row.text
                            color: row.kind === root.kindUser ? Theme.colorOnPrimary
                                 : row.kind === root.kindError ? Theme.colorBad
                                 : Theme.colorText
                        }
                    }
                }

                // Reasoning ("thinking"): a collapsed-by-default disclosure (committed this turn).
                ReasoningDisclosure {
                    id: reasoning
                    width: parent.width
                    visible: row.kind === root.kindReasoning
                    text: row.text
                }
            }

            // Live, in-progress reasoning (collapsible) + assistant text (+ a pulsing node while
            // it is still streaming).
            footer: ColumnLayout {
                width: list.width
                visible: root.agent.busy || root.agent.streamingText.length > 0
                         || root.agent.streamingReasoning.length > 0
                spacing: Theme.spacingXs

                ReasoningDisclosure {
                    Layout.fillWidth: true
                    visible: root.agent.streamingReasoning.length > 0
                    text: root.agent.streamingReasoning
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    PulsingNode {
                        running: root.agent.busy
                        Layout.alignment: Qt.AlignTop
                    }
                    MarkdownLabel {
                        Layout.fillWidth: true
                        color: Theme.colorText
                        text: root.agent.streamingText.length > 0 ? root.agent.streamingText
                                                                  : qsTr("Thinking…")
                    }
                }
            }
        }

        // Confirmation banner: the agent wants to write a journal entry — the user decides.
        Rectangle {
            Layout.fillWidth: true
            visible: root.agent.pendingConfirmation
            radius: Theme.radius
            color: Theme.colorCard
            border.width: 1
            border.color: Theme.colorAI
            implicitHeight: confirmRow.implicitHeight + 2 * Theme.spacingSm
            RowLayout {
                id: confirmRow
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingSm
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.agent.confirmationSummary
                }
                Button { text: qsTr("Reject"); onClicked: root.agent.confirm(false) }
                Button { text: qsTr("Approve"); onClicked: root.agent.confirm(true) }
            }
        }

        // Non-blocking privacy notice: a remote endpoint sends your messages + plant context
        // off-device. Informational only — it never interrupts typing or sending (the user chose
        // the endpoint in Settings); local endpoints (Ollama) don't show it.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            visible: root.agent.ready && root.agent.endpointIsRemote
            Icon {
                icon.name: "info"
                icon.color: Theme.colorTextVariant
                icon.size: Theme.fontSizeCaption
                Layout.alignment: Qt.AlignTop
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                text: qsTr("Your messages and plant data are sent to your configured remote AI "
                           + "provider.")
            }
        }

        // Pre-send disclosure: when photos are part of this conversation and the endpoint is
        // remote, show exactly which images leave the device as thumbnails (ADR 0025 decision 4).
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            visible: root.agent.ready && root.agent.endpointIsRemote
                     && root.agent.outgoingImages.length > 0
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                font.pixelSize: Theme.fontSizeCaption
                text: qsTr("%n photo(s) sent to the remote provider:", "", root.agent.outgoingImages.length)
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                Repeater {
                    model: root.agent.outgoingImages
                    delegate: Image {
                        required property string modelData
                        source: modelData
                        sourceSize.width: 64
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                }
            }
        }

        // Input row: ask a question, or cancel an in-flight turn. Multi-line composer —
        // Enter sends, Shift+Enter inserts a newline (usual chat convention).
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            ScrollView {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                // Grow from one line up to ~5 lines, then scroll internally.
                Layout.maximumHeight: Math.round(Theme.fontSizeBody * 8)
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                TextArea {
                    id: input
                    enabled: root.agent.ready && !root.agent.busy
                    placeholderText: qsTr("Ask about your plants…")
                    // Enter/Return sends; Shift+Enter inserts a newline. Handle both the
                    // main Return and the numeric-keypad Enter.
                    Keys.onReturnPressed: (event) => root.handleReturn(event)
                    Keys.onEnterPressed: (event) => root.handleReturn(event)
                }
            }
            Button {
                visible: !root.agent.busy
                text: qsTr("Send")
                Layout.alignment: Qt.AlignBottom
                enabled: root.agent.ready && input.text.trim().length > 0
                onClicked: root.send()
            }
            Button {
                visible: root.agent.busy
                text: qsTr("Stop")
                Layout.alignment: Qt.AlignBottom
                onClicked: root.agent.cancel()
            }
        }
    }

    // Enter sends; Shift+Enter falls through to the default handler (inserts a newline).
    function handleReturn(event) {
        if (event.modifiers & Qt.ShiftModifier) {
            event.accepted = false;
            return;
        }
        event.accepted = true; // consume — never insert a newline on a plain Enter
        root.send();           // send() no-ops on empty/whitespace
    }

    function send() {
        const text = input.text.trim();
        if (text.length === 0)
            return;
        root.agent.sendMessage(text);
        input.clear();
    }

    // Keep the newest content in view as it streams in.
    Connections {
        target: root.agent
        function onStreamingTextChanged() { Qt.callLater(list.positionViewAtEnd); }
        function onStreamingReasoningChanged() { Qt.callLater(list.positionViewAtEnd); }
    }
}
