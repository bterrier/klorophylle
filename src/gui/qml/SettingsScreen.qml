// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ScrollView/ScrollBar (no styled variant); styled controls below still win
import QtQuick.Layouts
import QtQuick.Dialogs
import Klorophylle.Style
import Klorophylle

// App-wide settings (reached from the nav rail's "More"): the colour scheme, the
// display-unit preferences, and care notifications. Each control binds the
// persisted `Settings` singleton — no logic here; conversion + judgment happen in C++.
// (Locale/i18n still deferred.)
Item {
    id: root
    property string title: qsTr("Settings")
    // Preset history-sync intervals (hours) the cadence ComboBox offers.
    readonly property var syncIntervals: [1, 3, 6, 12, 24]

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

        SectionHeader { text: qsTr("Appearance"); Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Label {
                text: qsTr("Colour scheme")
                Layout.fillWidth: true
            }
            ComboBox {
                // SettingsStore persists the choice; Main.qml binds Theme to it.
                // Order matches ThemeController.ColorScheme: 0 = Light, 1 = Dark, 2 = Auto.
                model: [qsTr("Light"), qsTr("Dark"), qsTr("Auto")]
                currentIndex: Settings.colorScheme
                onActivated: (index) => Settings.colorScheme = index
            }
        }

        SectionHeader { text: qsTr("Units"); Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Label {
                text: qsTr("Temperature")
                Layout.fillWidth: true
            }
            ComboBox {
                // klr::TemperatureUnit: 0 = Celsius, 1 = Fahrenheit.
                model: ["°C", "°F"]
                currentIndex: Settings.temperatureUnit
                onActivated: (index) => Settings.temperatureUnit = index
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Label {
                text: qsTr("Light")
                Layout.fillWidth: true
            }
            ComboBox {
                // klr::IlluminanceUnit: 0 = Lux, 1 = Micromole (µmol).
                model: ["lux", "µmol"]
                currentIndex: Settings.illuminanceUnit
                onActivated: (index) => Settings.illuminanceUnit = index
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Label {
                text: qsTr("Pressure")
                Layout.fillWidth: true
            }
            ComboBox {
                // klr::PressureUnit: 0 = Hectopascal, 1 = InchHg, 2 = MmHg.
                model: ["hPa", "inHg", "mmHg"]
                currentIndex: Settings.pressureUnit
                onActivated: (index) => Settings.pressureUnit = index
            }
        }

        SectionHeader { text: qsTr("Notifications"); Layout.fillWidth: true }

        // Care alerts: a desktop notification when a plant crosses a care threshold —
        // notably soil moisture dropping too low ("time to water"). The judgment + debounce
        // live in C++ (AlertController); here we only toggle and snooze.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Label {
                text: qsTr("Care notifications")
                Layout.fillWidth: true
            }
            Switch {
                checked: Settings.notificationsEnabled
                onToggled: Settings.notificationsEnabled = checked
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            enabled: Settings.notificationsEnabled
            Label {
                text: qsTr("Snooze for")
                Layout.fillWidth: true
            }
            Button { text: qsTr("1h"); onClicked: AppContext.snoozeNotifications(1) }
            Button { text: qsTr("8h"); onClicked: AppContext.snoozeNotifications(8) }
            Button { text: qsTr("24h"); onClicked: AppContext.snoozeNotifications(24) }
        }

        // Only shown while a snooze is active (text is empty otherwise — computed in C++).
        Label {
            Layout.fillWidth: true
            visible: AppContext.notificationsSnoozedText.length > 0
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeCaption
            text: AppContext.notificationsSnoozedText
        }

        SectionHeader { text: qsTr("Sensors"); Layout.fillWidth: true }

        // History backfill: the app connects on a cadence to download the hours it was
        // closed (and refresh battery, which Flower Care never broadcasts). Each connect costs
        // sensor battery, so the interval bounds how often that happens.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Label {
                text: qsTr("Download sensor history")
                Layout.fillWidth: true
            }
            Switch {
                checked: Settings.historySyncEnabled
                onToggled: Settings.historySyncEnabled = checked
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            enabled: Settings.historySyncEnabled
            Label {
                text: qsTr("Sync at most every")
                Layout.fillWidth: true
            }
            ComboBox {
                model: [qsTr("1 hour"), qsTr("3 hours"), qsTr("6 hours"),
                        qsTr("12 hours"), qsTr("24 hours")]
                currentIndex: Math.max(0, root.syncIntervals.indexOf(Settings.historySyncIntervalHours))
                onActivated: (index) => Settings.historySyncIntervalHours = root.syncIntervals[index]
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Button {
                text: AppContext.historySyncing ? qsTr("Syncing history…") : qsTr("Sync history now")
                enabled: !AppContext.historySyncing
                onClicked: AppContext.syncHistoryNow()
            }
            Item { Layout.fillWidth: true }
        }

        SectionHeader { text: qsTr("AI assistant"); Layout.fillWidth: true }

        // The AI chat. Non-secret config (endpoint, model) lives in Settings; the API
        // key goes through the secret store, never QSettings (ADR 0019). Defaults target a local
        // Ollama, which needs no key.
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

        SectionHeader { text: qsTr("Data"); Layout.fillWidth: true }

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
