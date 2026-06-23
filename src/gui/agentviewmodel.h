// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agenttools.h"
#include "confirmingtool.h"
#include "contextbuilder.h"
#include "ids.h"
#include "providerconfig.h" // karness::ProviderConfig (provider factory input)

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

namespace karness {
class IProvider;
class AgentSession;
struct ToolResultBlock;
struct Message;
struct AgentError;
enum class StopReason;
}

namespace klr {

class IPlantRepository;
class IJournalRepository;
class IAttachmentRepository;
class IAttachmentFileStore;
class IBindingRepository;
class IReadingRepository;
class ICareThresholdRepository;
class IAgentRepository;
class ISecretStore;
class IWebFetcher;
class SettingsStore;
class Clock;

// The chat view-model behind the AIInsights screen (ADR 0019, decision 7). A QAbstractListModel
// of transcript rows that drives a karness::AgentSession over the four domain tools + the
// add_journal_entry confirmation decorator, persists finished turns via klr_agent's Transcript,
// and re-renders the deterministic context block as the per-turn system prompt.
//
// The provider is built behind an injected factory (production -> OpenAiCompatProvider, tests ->
// FakeProvider) so the whole loop is headlessly testable. Built lazily and rebuilt when the
// SettingsStore endpoint/model/key change. No SQL, no setContextProperty — AppContext hands this
// to QML. Cyan (colorAI) is the screen's accent (ADR 0013 #5); reasoning UI lands later, not here.
class AgentViewModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AgentViewModel is provided by AppContext.agent")
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // The in-progress assistant text streamed this turn (committed rows arrive on turnFinished).
    Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingTextChanged)
    // The in-progress reasoning ("thinking") streamed this turn, shown collapsibly while live;
    // the committed ReasoningKind row arrives on turnFinished (ADR 0019 decision 6).
    Q_PROPERTY(QString streamingReasoning READ streamingReasoning NOTIFY streamingReasoningChanged)
    Q_PROPERTY(bool pendingConfirmation READ pendingConfirmation NOTIFY confirmationChanged)
    Q_PROPERTY(QString confirmationSummary READ confirmationSummary NOTIFY confirmationChanged)
    // Whether the configured endpoint is remote (not localhost): messages + plant context leave
    // the device. Drives a non-blocking inline notice in the chat — NOT a consent gate (the user
    // configured the endpoint). Re-evaluated on a config change (which also fires readyChanged).
    Q_PROPERTY(bool endpointIsRemote READ endpointIsRemote NOTIFY readyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY apiKeyChanged)
    // The photos currently in the conversation (data: URLs) — what a remote provider has received /
    // will receive this turn. The pre-send disclosure shows these as thumbnails so the user sees
    // exactly which images leave the device (ADR 0025 decision 4).
    Q_PROPERTY(QStringList outgoingImages READ outgoingImages NOTIFY outgoingImagesChanged)

public:
    enum RowKind { UserKind = 0, AssistantKind, ToolCallKind, ToolResultKind, ErrorKind, ReasoningKind };
    Q_ENUM(RowKind)

    enum Role {
        KindRole = Qt::UserRole + 1, // RowKind (int)
        TextRole,                    // user/assistant/error text or tool-result text
        ToolNameRole,                // tool name for ToolCall/ToolResult rows
        StreamingRole,               // assistant row currently being streamed
        ImagesRole,                  // QStringList of data: URLs for a tool-result row's photos
    };

    // Builds the provider for a given dialect (SettingsStore::agentProviderType:
    // 0=OpenAI-compatible, 1=OpenAI Responses, 2=Anthropic, 3=Gemini) over the shared
    // transport config. Production switches on the type; tests return a FakeProvider.
    using ProviderFactory =
        std::function<std::unique_ptr<karness::IProvider>(int providerType,
                                                          const karness::ProviderConfig &)>;

    AgentViewModel(const IPlantRepository &plants, IJournalRepository &journal,
                   const IBindingRepository &bindings, const IReadingRepository &readings,
                   const ICareThresholdRepository &thresholds, const Clock &clock,
                   SettingsStore &settings, ISecretStore &secrets, IAgentRepository &transcripts,
                   IWebFetcher &webFetcher, const IAttachmentRepository &attachments,
                   const IAttachmentFileStore &fileStore, ProviderFactory factory = {},
                   QObject *parent = nullptr);
    ~AgentViewModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool busy() const;
    bool ready() const;
    QString streamingText() const { return m_streamingText; }
    QString streamingReasoning() const { return m_streamingReasoning; }
    bool pendingConfirmation() const { return m_pendingConfirmation; }
    QString confirmationSummary() const { return m_confirmationSummary; }
    // A non-localhost endpoint — messages + plant context leave the device (drives the inline notice).
    bool endpointIsRemote() const;
    QString lastError() const { return m_lastError; }
    bool hasApiKey() const;
    QStringList outgoingImages() const { return m_outgoingImages; }

    Q_INVOKABLE void sendMessage(const QString &text);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void confirm(bool approved);
    Q_INVOKABLE void startNewConversation();
    // Store (or clear, when empty) the remote-provider API key via the ISecretStore seam — never
    // QSettings (ADR 0019 decision 9). Rebuilds the provider on the next send so the key takes.
    Q_INVOKABLE void setApiKey(const QString &key);
    // The onboarding descriptor for a provider-type index as a plain map for QML (ADR 0027): the
    // AI settings screen reads fixedEndpoint/needsKey/keyUrl/knownModels/… off it to adapt fields,
    // seed model autocomplete and show key/free-tier links. A thin bridge over providerDescriptor().
    Q_INVOKABLE QVariantMap providerDescriptor(int type) const;

signals:
    void busyChanged();
    void readyChanged();
    void streamingTextChanged();
    void streamingReasoningChanged();
    void confirmationChanged();
    void lastErrorChanged();
    void apiKeyChanged();
    void outgoingImagesChanged();

private:
    struct ChatRow {
        RowKind kind = UserKind;
        QString text;
        QString toolName;
        bool streaming = false;
        QStringList images; // data: URLs for a tool-result row's photos, else empty
    };

    void ensureSession();
    // The provider's effective base URL: the descriptor's fixed endpoint for a cloud provider,
    // else the user-configured agentBaseUrl (ADR 0027). Drives both the provider config and the
    // remote-endpoint notice, so a fixed cloud endpoint is recognised as remote.
    QUrl effectiveBaseUrl() const;
    void doSend(const QString &text);
    void onTextDelta(const QString &text);
    void onReasoningDelta(const QString &text);
    void onTurnFinished(karness::StopReason reason);
    void onTurnFailed(const karness::AgentError &error);
    void onConfirmationRequested(const QString &summary);

    QString buildInstructions() const;    // the stable system prompt (tool guidance + unit prefs)
    QString buildUnitPreferences() const; // the user's unit-preference sentence (stable-for-session)
    void appendRow(ChatRow row);
    void setStreamingText(const QString &text);
    void setStreamingReasoning(const QString &text);
    void appendRowsFor(const karness::Message &message); // render a message into committed rows
    void refreshOutgoingImages(); // recompute m_outgoingImages from the session history
    void persistNewTurn();
    void setLastError(const QString &error);
    void setPendingConfirmation(bool pending, const QString &summary = {});

    // injected dependencies
    const IPlantRepository &m_plants;
    IJournalRepository &m_journal;
    const Clock &m_clock;
    SettingsStore &m_settings;
    ISecretStore &m_secrets;
    IAgentRepository &m_transcripts;
    ProviderFactory m_factory;

    // owned tool surface (each holds repo refs; the decorator wraps the write tool)
    ListPlantsTool m_listTool;
    ReadPlantJournalTool m_journalTool;
    ReadPlantDataTool m_dataTool;
    AddJournalEntryTool m_addTool;
    ConfirmingTool m_confirmTool;
    SetPlantMemoryTool m_setMemoryTool; // agent memory: registered UNwrapped — no confirmation
    SetGlobalMemoryTool m_setGlobalMemoryTool; // global memory: also UNwrapped (decision 2)
    ReadGlobalMemoryTool m_readGlobalMemoryTool; // global memory read path
    ReadOnlinePlantDbTool m_webTool; // web lookup: registered only when the opt-in setting is on
    ReadPlantPhotoTool m_photoTool;  // vision: registered only when agentVisionEnabled is on
    ContextBuilder m_context;

    std::unique_ptr<karness::IProvider> m_provider;
    std::unique_ptr<karness::AgentSession> m_session;
    bool m_sessionDirty = true; // rebuild provider+session on next send (config changed)

    ConversationId m_conversation;
    int m_persistedCount = 0; // session.history() messages already written to the transcript
    int m_turnBaseIndex = 0;  // session.history() size when the current turn was sent

    QList<ChatRow> m_rows;
    QHash<QString, QString> m_callNames; // callId -> tool name (for ToolResult rows)
    QString m_streamingText;             // live partial assistant text for the current turn
    QString m_streamingReasoning;        // live partial reasoning ("thinking") for the current turn
    bool m_pendingConfirmation = false;
    QString m_confirmationSummary;
    QString m_lastError;
    QStringList m_outgoingImages; // data: URLs of photos in the conversation (disclosure)
};

} // namespace klr
