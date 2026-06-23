// SPDX-License-Identifier: GPL-3.0-or-later
#include "agentviewmodel.h"

#include "agentsession.h"
#include "anthropicprovider.h"
#include "clock.h"
#include "format.h" // klr::unitSymbol
#include "geminiprovider.h"
#include "iagentrepository.h"
#include "inferencerequest.h"
#include "isecretstore.h"
#include "iwebfetcher.h"
#include "message.h"
#include "openaicompatprovider.h"
#include "providerdescriptor.h"
#include "responsesprovider.h"
#include "settingsstore.h"
#include "transcript.h"
#include "units.h" // klr::displayUnit + DisplayUnits

#include <QtCore/QDateTime>
#include <QtCore/QTimeZone>
#include <QtCore/QUrl>

#include <algorithm>

namespace klr {

namespace {

// A base64 data: URL for an image block — what QML Image { source } binds to.
QString imageDataUrl(const karness::ImageBlock &image)
{
    const QString mime = image.mimeType.isEmpty() ? QStringLiteral("image/jpeg") : image.mimeType;
    return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(image.data.toBase64()));
}

// A human line for one tool result — text parts joined (images render separately).
QString toolResultText(const karness::ToolResultBlock &result)
{
    QString out;
    for (const karness::ContentPart &part : result.parts) {
        if (const auto *t = std::get_if<karness::TextBlock>(&part)) {
            if (!out.isEmpty())
                out += QLatin1Char('\n');
            out += t->text;
        }
    }
    return out;
}

// The data: URLs of the image parts of one tool result.
QStringList toolResultImages(const karness::ToolResultBlock &result)
{
    QStringList urls;
    for (const karness::ContentPart &part : result.parts)
        if (const auto *image = std::get_if<karness::ImageBlock>(&part))
            urls.append(imageDataUrl(*image));
    return urls;
}

QString humanError(const karness::AgentError &error)
{
    QString base;
    switch (error.code) {
    case karness::AgentError::Code::Network:
        base = QObject::tr("Could not reach the AI endpoint.");
        break;
    case karness::AgentError::Code::Http:
        base = QObject::tr("The AI endpoint returned an error (HTTP %1).")
                   .arg(error.httpStatus.value_or(0));
        break;
    case karness::AgentError::Code::Provider:
        base = QObject::tr("The AI provider reported an error.");
        break;
    case karness::AgentError::Code::Parse:
        base = QObject::tr("The AI response could not be understood.");
        break;
    case karness::AgentError::Code::Cancelled:
        base = QObject::tr("Cancelled.");
        break;
    case karness::AgentError::Code::Timeout:
        base = QObject::tr("The AI took too long to respond.");
        break;
    case karness::AgentError::Code::LoopLimit:
        base = QObject::tr("The assistant made too many tool calls and stopped.");
        break;
    case karness::AgentError::Code::NotReady:
        base = QObject::tr("No AI endpoint is configured.");
        break;
    }
    return error.message.isEmpty() ? base : base + QStringLiteral(" (") + error.message + QLatin1Char(')');
}

} // namespace

AgentViewModel::AgentViewModel(const IPlantRepository &plants, IJournalRepository &journal,
                               const IBindingRepository &bindings, const IReadingRepository &readings,
                               const ICareThresholdRepository &thresholds, const Clock &clock,
                               SettingsStore &settings, ISecretStore &secrets,
                               IAgentRepository &transcripts, IWebFetcher &webFetcher,
                               const IAttachmentRepository &attachments,
                               const IAttachmentFileStore &fileStore, ProviderFactory factory,
                               QObject *parent)
    : QAbstractListModel(parent)
    , m_plants(plants)
    , m_journal(journal)
    , m_clock(clock)
    , m_settings(settings)
    , m_secrets(secrets)
    , m_transcripts(transcripts)
    , m_factory(std::move(factory))
    , m_listTool(plants, clock)
    , m_journalTool(plants, journal)
    , m_dataTool(plants, bindings, readings, thresholds, clock)
    , m_addTool(plants, journal, clock)
    , m_confirmTool(m_addTool, [](const QJsonObject &args) {
        const QString note = args.value(QStringLiteral("note")).toString();
        const QString kind = args.value(QStringLiteral("kind")).toString();
        return AgentViewModel::tr("Save a %1 journal entry: %2")
            .arg(kind.isEmpty() ? QStringLiteral("Note") : kind, note);
    })
    , m_setMemoryTool(plants, journal, clock)
    , m_setGlobalMemoryTool(journal, clock)
    , m_readGlobalMemoryTool(journal)
    , m_webTool(webFetcher)
    , m_photoTool(plants, journal, attachments, fileStore)
    , m_context(plants)
{
    if (!m_factory) {
        m_factory = [](int providerType,
                       const karness::ProviderConfig &cfg) -> std::unique_ptr<karness::IProvider> {
            // SettingsStore::agentProviderType order. Native dialects (Responses/Anthropic/Gemini)
            // wire their own case as each lands; until then they fall through to compat.
            switch (providerType) {
            case 1:
                return std::make_unique<karness::ResponsesProvider>(cfg);
            case 2:
                return std::make_unique<karness::AnthropicProvider>(cfg);
            case 3:
                return std::make_unique<karness::GeminiProvider>(cfg);
            default:
                return std::make_unique<karness::OpenAiCompatProvider>(cfg);
            }
        };
    }

    connect(&m_confirmTool, &ConfirmingTool::confirmationRequested, this,
            [this](const QString &summary, const QJsonObject &) { onConfirmationRequested(summary); });
    connect(&m_settings, &SettingsStore::agentChanged, this, [this] {
        m_sessionDirty = true; // pick up the new endpoint/model/key on the next send
        emit readyChanged();
    });
    // Unit preferences live in the stable system prompt, so a unit change must rebuild
    // the session (new prefix) — the cache-safe "new prompt => new session" rule.
    connect(&m_settings, &SettingsStore::unitsChanged, this, [this] { m_sessionDirty = true; });

    // Resume the newest conversation (display only — the LLM context starts fresh), or open one.
    const QList<AgentConversation> existing = m_transcripts.conversations();
    if (!existing.isEmpty()) {
        m_conversation = existing.first().id;
        for (const karness::Message &msg : transcript::load(m_transcripts, m_conversation))
            appendRowsFor(msg);
    } else {
        const AgentConversation conv{ ConversationId::generate(),
                                      QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC),
                                      {} };
        m_transcripts.createConversation(conv);
        m_conversation = conv.id;
    }
}

AgentViewModel::~AgentViewModel() = default;

void AgentViewModel::ensureSession()
{
    if (m_session && !m_sessionDirty)
        return;

    karness::ProviderConfig cfg;
    cfg.baseUrl = effectiveBaseUrl();
    if (const std::optional<QString> key = m_secrets.secret(QStringLiteral("agent/apiKey")))
        cfg.apiKey = *key;
    cfg.caps = karness::ModelCaps{ /*toolCalling*/ true,
                                   /*vision*/ m_settings.agentVisionEnabled(), // user opt-in
                                   /*reasoning*/ true, 0 };

    m_provider = m_factory(m_settings.agentProviderType(), cfg);
    // The system prompt is fixed for the session's life (stable cacheable prefix); the
    // volatile context block is delivered per turn via setAmbient in doSend(). A tools
    // on/off toggle changes the instructions, but that emits agentChanged → m_sessionDirty,
    // so the session is rebuilt with fresh instructions — no drift.
    m_session = std::make_unique<karness::AgentSession>(*m_provider, buildInstructions());
    if (m_settings.agentToolsEnabled()) {
        QList<karness::ITool *> tools{ &m_listTool, &m_journalTool, &m_dataTool, &m_confirmTool,
                                       &m_setMemoryTool, // memory write is unconfirmed (decision 3)
                                       &m_setGlobalMemoryTool, // global memory, also unconfirmed
                                       &m_readGlobalMemoryTool };
        // Web lookup is opt-in (ADR 0023): registered only when the user enables it.
        if (m_settings.agentWebToolEnabled())
            tools.append(&m_webTool);
        // Vision is opt-in (ADR 0025): the photo tool is registered only when enabled, so an
        // ImageBlock never reaches a dialect with vision off (the dialects encode unconditionally).
        if (m_settings.agentVisionEnabled())
            tools.append(&m_photoTool);
        m_session->setTools(tools);
    }
    karness::ModelConfig modelCfg;
    modelCfg.model = m_settings.agentModel();
    // The effort knob: an int matching ReasoningEffort order; the compat dialect maps it to
    // "reasoning_effort" on the wire (omitted when Off). Native dialects map it per-provider.
    modelCfg.reasoningEffort = static_cast<karness::ReasoningEffort>(
        std::clamp(m_settings.agentReasoningEffort(), 0, 3));
    m_session->setModelConfig(modelCfg);

    // Deltas drive the live streaming preview; committed rows are rendered authoritatively from
    // history() on turnFinished (so text that arrives only in the terminal Done still appears).
    connect(m_session.get(), &karness::AgentSession::textDelta, this, &AgentViewModel::onTextDelta);
    connect(m_session.get(), &karness::AgentSession::reasoningDelta, this,
            &AgentViewModel::onReasoningDelta);
    connect(m_session.get(), &karness::AgentSession::turnFinished, this,
            &AgentViewModel::onTurnFinished);
    connect(m_session.get(), &karness::AgentSession::turnFailed, this,
            &AgentViewModel::onTurnFailed);
    connect(m_session.get(), &karness::AgentSession::busyChanged, this,
            &AgentViewModel::busyChanged);

    m_sessionDirty = false;

    // Resume context: seed the fresh session with the conversation's already-persisted messages,
    // so a reopened conversation (or one whose session was rebuilt after a settings change)
    // continues with full model context — not just restored display rows. They are already
    // stored, so they form the persisted baseline; only NEW turns are appended by
    // persistNewTurn(). The system prompt stays the session's construction-time invariant.
    QList<karness::Message> priorHistory = transcript::load(m_transcripts, m_conversation);
    m_persistedCount = static_cast<int>(priorHistory.size());
    m_session->primeHistory(std::move(priorHistory));
}

int AgentViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant AgentViewModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const ChatRow &row = m_rows.at(index.row());
    switch (role) {
    case KindRole:
        return static_cast<int>(row.kind);
    case TextRole:
        return row.text;
    case ToolNameRole:
        return row.toolName;
    case StreamingRole:
        return row.streaming;
    case ImagesRole:
        return row.images;
    default:
        return {};
    }
}

QHash<int, QByteArray> AgentViewModel::roleNames() const
{
    return {
        { KindRole, "kind" },
        { TextRole, "text" },
        { ToolNameRole, "toolName" },
        { StreamingRole, "streaming" },
        { ImagesRole, "images" },
    };
}

bool AgentViewModel::busy() const
{
    return m_session && m_session->busy();
}

bool AgentViewModel::ready() const
{
    return m_settings.agentEnabled() && !m_settings.agentModel().isEmpty();
}

bool AgentViewModel::hasApiKey() const
{
    return m_secrets.secret(QStringLiteral("agent/apiKey")).has_value();
}

void AgentViewModel::setApiKey(const QString &key)
{
    if (key.isEmpty())
        m_secrets.removeSecret(QStringLiteral("agent/apiKey"));
    else
        m_secrets.setSecret(QStringLiteral("agent/apiKey"), key);
    m_sessionDirty = true; // the next send rebuilds the provider with the new key
    emit apiKeyChanged();
}

void AgentViewModel::sendMessage(const QString &text)
{
    if (text.trimmed().isEmpty() || busy())
        return;
    if (!ready()) {
        setLastError(tr("The AI assistant is turned off, or no model is configured in Settings."));
        return;
    }
    // A remote endpoint sends plant context off-device, but that is surfaced as a non-blocking
    // inline notice in the chat (endpointIsRemote), not a pre-send consent gate — the user chose
    // the endpoint in Settings. No interruption between typing and sending.
    doSend(text);
}

void AgentViewModel::doSend(const QString &text)
{
    ensureSession();
    setLastError({});
    m_turnBaseIndex = static_cast<int>(m_session->history().size());
    appendRow(ChatRow{ UserKind, text, {}, false });
    // Deliver the deterministic context block at the turn tail (re-read fresh each send),
    // keeping the system prompt + tools a stable cacheable prefix.
    m_session->setAmbient(m_context.build());

    const std::expected<void, karness::AgentError> sent = m_session->send(text);
    if (!sent) {
        onTurnFailed(sent.error());
        return;
    }
    emit busyChanged();
}

QUrl AgentViewModel::effectiveBaseUrl() const
{
    const ProviderDescriptor &d = klr::providerDescriptor(m_settings.agentProviderType());
    return d.fixedEndpoint.isEmpty() ? QUrl(m_settings.agentBaseUrl()) : QUrl(d.fixedEndpoint);
}

QVariantMap AgentViewModel::providerDescriptor(int type) const
{
    const ProviderDescriptor &d = klr::providerDescriptor(type);
    return QVariantMap{
        { QStringLiteral("displayName"), d.displayName },
        { QStringLiteral("fixedEndpoint"), d.fixedEndpoint },
        { QStringLiteral("needsKey"), d.needsKey },
        { QStringLiteral("keyUrl"), d.keyUrl },
        { QStringLiteral("defaultModel"), d.defaultModel },
        { QStringLiteral("knownModels"), d.knownModels },
        { QStringLiteral("textOnlyModels"), d.textOnlyModels },
        { QStringLiteral("freeTierUrl"), d.freeTierUrl },
    };
}

bool AgentViewModel::endpointIsRemote() const
{
    const QString host = effectiveBaseUrl().host();
    return !(host.isEmpty() || host == QStringLiteral("localhost")
             || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1"));
}

void AgentViewModel::cancel()
{
    if (m_session)
        m_session->cancel();
}

void AgentViewModel::confirm(bool approved)
{
    if (!m_pendingConfirmation)
        return;
    setPendingConfirmation(false);
    if (approved)
        m_confirmTool.approve();
    else
        m_confirmTool.reject();
}

void AgentViewModel::startNewConversation()
{
    if (busy())
        return;
    beginResetModel();
    m_rows.clear();
    m_callNames.clear();
    endResetModel();

    const AgentConversation conv{ ConversationId::generate(),
                                  QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC),
                                  {} };
    m_transcripts.createConversation(conv);
    m_conversation = conv.id;
    m_session.reset();
    m_provider.reset();
    m_sessionDirty = true;
    m_persistedCount = 0;
    setPendingConfirmation(false);
    m_confirmTool.reset(); // drop any confirmations queued from the abandoned conversation
    setLastError({});
    refreshOutgoingImages(); // new conversation: no photos sent yet
}

QString AgentViewModel::buildInstructions() const
{
    // The stable system prompt (no volatile context — that flows through setAmbient per turn).
    // With tools on, point the model at them; with tools off it can only chat over the roster
    // delivered as ambient context, so don't instruct it to "use the tools" it doesn't have.
    const QString base = m_settings.agentToolsEnabled()
        ? tr("You are a helpful gardening assistant inside a plant-monitoring app. Use the tools "
             "to look up the user's plants and their sensor readings before answering, and refer "
             "to concrete values. To record care, call add_journal_entry — the user confirms it "
             "before it is saved. Remember durable per-plant facts (e.g. care habits, location, "
             "sensitivities) with set_plant_memory, and consult a plant's memory in its journal "
             "before advising — read its current memory first, then save the complete updated text. "
             "For facts that apply to every plant (e.g. the owner's water, climate, or travel "
             "habits), use set_global_memory and consult it with read_global_memory before "
             "advising — read the current global memory first, then save the complete updated text.")
        : tr("You are a helpful gardening assistant inside a plant-monitoring app. Answer using "
             "the plant list below; you have no tools, so don't claim to look anything up.");
    // Web lookup is opt-in; only name the tool when the user has enabled it.
    const QString web = (m_settings.agentToolsEnabled() && m_settings.agentWebToolEnabled())
        ? QStringLiteral(" ") + tr("For background or care information you don't already have, you "
                                   "may look a species up online with read_online_plant_db "
                                   "(Wikipedia or Wikispecies).")
        : QString();
    // Vision is opt-in; only name the photo tool when the user has enabled it.
    const QString vision = (m_settings.agentToolsEnabled() && m_settings.agentVisionEnabled())
        ? QStringLiteral(" ") + tr("When diagnosing a visible problem, call read_plant_photo to look "
                                   "at the plant's journal photos.")
        : QString();
    return base + web + vision + QStringLiteral("\n\n") + buildUnitPreferences();
}

QString AgentViewModel::buildUnitPreferences() const
{
    // Static, stable-for-session context (a unit change rebuilds the session via unitsChanged).
    // Advisory phrasing only — tools return canonical values; this keeps the model's prose in the
    // user's units. Reuse the klr_core display-unit selector + symbol formatter.
    const DisplayUnits units = m_settings.displayUnits();
    const QString temp = unitSymbol(displayUnit(Quantity::AirTemperature, units));
    const QString light = unitSymbol(displayUnit(Quantity::Illuminance, units));
    const QString pressure = unitSymbol(displayUnit(Quantity::Pressure, units));
    return tr("State temperatures in %1, light in %2, and pressure in %3.").arg(temp, light, pressure);
}

void AgentViewModel::onTextDelta(const QString &text)
{
    setStreamingText(m_streamingText + text);
}

void AgentViewModel::onReasoningDelta(const QString &text)
{
    setStreamingReasoning(m_streamingReasoning + text);
}

void AgentViewModel::onTurnFinished(karness::StopReason)
{
    setStreamingText({});
    setStreamingReasoning({});
    // Render the messages this turn added, skipping the user message already shown live.
    const QList<karness::Message> &history = m_session->history();
    for (int i = m_turnBaseIndex + 1; i < history.size(); ++i)
        appendRowsFor(history.at(i));
    persistNewTurn();
    refreshOutgoingImages(); // a read_plant_photo this turn means photos were sent (disclosure)
    emit busyChanged();
}

void AgentViewModel::onTurnFailed(const karness::AgentError &error)
{
    setStreamingText({});
    setStreamingReasoning({});
    // Log the raw, untruncated provider error to the terminal: the in-chat row is humanized and
    // (today) unselectable, so this is the copy-pasteable record for diagnosing provider failures
    // (e.g. a Gemini "Unknown name" schema rejection). Friendlier in-UI surfacing is a follow-up.
    qWarning("AgentViewModel turn failed: code=%d httpStatus=%d message=%s", int(error.code),
             error.httpStatus.value_or(0), qUtf8Printable(error.message));
    const QString message = humanError(error);
    appendRow(ChatRow{ ErrorKind, message, {}, false });
    setLastError(message);
    setPendingConfirmation(false);
    // A cancelled/failed/timed-out turn may have left queued confirmations unresolved; clear
    // them so they don't suppress the next turn's first prompt.
    m_confirmTool.reset();
    emit busyChanged();
}

void AgentViewModel::onConfirmationRequested(const QString &summary)
{
    setPendingConfirmation(true, summary);
}

void AgentViewModel::appendRow(ChatRow row)
{
    beginInsertRows({}, static_cast<int>(m_rows.size()), static_cast<int>(m_rows.size()));
    m_rows.append(std::move(row));
    endInsertRows();
}

void AgentViewModel::setStreamingText(const QString &text)
{
    if (m_streamingText == text)
        return;
    m_streamingText = text;
    emit streamingTextChanged();
}

void AgentViewModel::setStreamingReasoning(const QString &text)
{
    if (m_streamingReasoning == text)
        return;
    m_streamingReasoning = text;
    emit streamingReasoningChanged();
}

void AgentViewModel::appendRowsFor(const karness::Message &message)
{
    using namespace karness;
    for (const ContentBlock &block : message.blocks) {
        if (const auto *t = std::get_if<TextBlock>(&block)) {
            const RowKind kind = message.role == karness::Role::User ? UserKind : AssistantKind;
            if (!t->text.isEmpty())
                appendRow(ChatRow{ kind, t->text, {}, false });
        } else if (const auto *call = std::get_if<ToolCallBlock>(&block)) {
            m_callNames.insert(call->id, call->name);
            appendRow(ChatRow{ ToolCallKind, {}, call->name, false });
        } else if (const auto *res = std::get_if<ToolResultBlock>(&block)) {
            // A tool-result row carries its photos (data: URLs) for the transcript thumbnails.
            appendRow(ChatRow{ ToolResultKind, toolResultText(*res), m_callNames.value(res->callId),
                               false, toolResultImages(*res) });
        } else if (const auto *reason = std::get_if<ReasoningBlock>(&block)) {
            if (!reason->text.isEmpty())
                appendRow(ChatRow{ ReasoningKind, reason->text, {}, false });
        }
    }
}

void AgentViewModel::refreshOutgoingImages()
{
    using namespace karness;
    QStringList urls;
    if (m_session) {
        for (const Message &message : m_session->history())
            for (const ContentBlock &block : message.blocks) {
                if (const auto *image = std::get_if<ImageBlock>(&block))
                    urls.append(imageDataUrl(*image)); // a top-level image (future chat-attached)
                else if (const auto *res = std::get_if<ToolResultBlock>(&block))
                    urls += toolResultImages(*res);    // photos a tool returned this conversation
            }
    }
    if (urls == m_outgoingImages)
        return;
    m_outgoingImages = urls;
    Q_EMIT outgoingImagesChanged();
}

void AgentViewModel::persistNewTurn()
{
    if (!m_session)
        return;
    const QList<karness::Message> &history = m_session->history();
    if (history.size() <= m_persistedCount)
        return;
    transcript::appendAll(m_transcripts, m_conversation, history.mid(m_persistedCount), m_clock);
    m_persistedCount = static_cast<int>(history.size());
}

void AgentViewModel::setLastError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void AgentViewModel::setPendingConfirmation(bool pending, const QString &summary)
{
    if (m_pendingConfirmation == pending && m_confirmationSummary == summary)
        return;
    m_pendingConfirmation = pending;
    m_confirmationSummary = pending ? summary : QString();
    emit confirmationChanged();
}

} // namespace klr
