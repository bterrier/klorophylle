// SPDX-License-Identifier: GPL-3.0-or-later
#include "agentsession.h"

#include <QtCore/QTimer>

namespace karness {

AgentSession::AgentSession(IProvider &provider, QString systemPrompt, AgentSessionConfig config,
                           QObject *parent)
    : QObject(parent)
    , m_provider(provider)
    , m_config(config)
    , m_systemPrompt(std::move(systemPrompt))
{
    m_turnTimer.setSingleShot(true);
    connect(&m_turnTimer, &QTimer::timeout, this, &AgentSession::onTurnTimeout);
}

AgentSession::~AgentSession()
{
    if (!m_turn)
        return;
    ++m_generation;
    dropWatcher();
    m_provider.cancel(); // close the provider's turn; no signals from a dtor
}

void AgentSession::setTools(const QList<ITool *> &tools)
{
    m_tools.clear();
    m_toolSpecs.clear();
    for (ITool *tool : tools) {
        const ToolSpec spec = tool->spec();
        m_tools.insert(spec.name, tool);
        m_toolSpecs.append(spec);
    }
}

void AgentSession::setAmbient(QString ambient)
{
    m_ambient = std::move(ambient);
}

void AgentSession::setModelConfig(ModelConfig config)
{
    m_modelConfig = std::move(config);
}

std::expected<void, AgentError> AgentSession::send(const QString &userText)
{
    return send(Message{Role::User, {TextBlock{userText}}});
}

std::expected<void, AgentError> AgentSession::send(Message userMessage)
{
    if (m_turn)
        return std::unexpected(AgentError{AgentError::Code::Provider,
                                          QStringLiteral("a turn is already in flight"), {}});
    if (!m_provider.isReady())
        return std::unexpected(AgentError{AgentError::Code::NotReady,
                                          QStringLiteral("provider has no usable model/endpoint"),
                                          {}});

    m_turn = std::make_unique<Turn>();
    m_turn->historyBaseline = m_history.size();
    m_history.append(std::move(userMessage));
    if (m_config.turnTimeout.count() > 0)
        m_turnTimer.start(m_config.turnTimeout);
    emit busyChanged(true);
    startIteration();
    return {};
}

void AgentSession::cancel()
{
    if (!m_turn)
        return;
    failTurn(AgentError{AgentError::Code::Cancelled, QStringLiteral("turn cancelled"), {}},
             /*cancelProvider=*/true);
}

void AgentSession::onTurnTimeout()
{
    if (!m_turn)
        return;
    // Same fail-first teardown as cancel(): the terminal is Timeout, and
    // the provider's late Cancelled never gets to claim otherwise.
    failTurn(AgentError{AgentError::Code::Timeout,
                        QStringLiteral("turn exceeded its wall-clock budget"), {}},
             /*cancelProvider=*/true);
}

void AgentSession::startIteration()
{
    Turn &turn = *m_turn;
    ++turn.iterations;
    if (turn.iterations > m_config.maxIterations) {
        failTurn(AgentError{AgentError::Code::LoopLimit,
                            QStringLiteral("agent loop hit the iteration limit"), {}});
        return;
    }

    turn.calls.clear();
    turn.results.clear();
    turn.pendingTools = 0;

    dropWatcher(); // previous iteration's watcher, if any
    turn.watcher = std::make_unique<QFutureWatcher<StreamEvent>>();
    connect(turn.watcher.get(), &QFutureWatcherBase::resultReadyAt, this, [this](int index) {
        if (m_turn && m_turn->watcher)
            onStreamEvent(m_turn->watcher->resultAt(index));
    });
    turn.watcher->setFuture(m_provider.generate(buildRequest()));
}

void AgentSession::onStreamEvent(const StreamEvent &event)
{
    std::visit(
        [this](const auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, TextDelta>) {
                if (!e.text.isEmpty())
                    emit textDelta(e.text);
            } else if constexpr (std::is_same_v<T, ReasoningDelta>) {
                if (!e.text.isEmpty())
                    emit reasoningDelta(e.text);
            } else if constexpr (std::is_same_v<T, Done>) {
                handleDone(e);
            } else if constexpr (std::is_same_v<T, ErrorEvent>) {
                failTurn(e.error);
            }
            // ToolCallStart/ToolCallArgsDelta: ignored — Done carries the
            // assembled calls; the loop signals at invocation time.
        },
        event);
}

void AgentSession::handleDone(const Done &done)
{
    m_history.append(done.message);

    QList<ToolCallBlock> calls;
    for (const ContentBlock &block : done.message.blocks)
        if (const auto *call = std::get_if<ToolCallBlock>(&block))
            calls.append(*call);

    switch (done.stopReason) {
    case StopReason::MaxTokens:
    case StopReason::ContentFilter:
    case StopReason::Other:
        // Partial message kept and flagged via the reason; tools are never
        // run on these (args may be truncated mid-JSON).
        finishTurn(done.stopReason);
        return;
    case StopReason::EndTurn:
    case StopReason::ToolCalls:
        break;
    }

    // Loop on the PRESENCE of calls, not the reason alone — compat servers
    // return finish_reason "stop" alongside tool calls (Ollama reality).
    if (!calls.isEmpty()) {
        m_turn->calls = std::move(calls);
        runTools();
        return;
    }
    if (done.stopReason == StopReason::ToolCalls) {
        // The inverse violation: nothing to run, nothing to show — loud.
        failTurn(AgentError{AgentError::Code::Parse,
                            QStringLiteral("stop reason ToolCalls without tool calls"), {}});
        return;
    }
    finishTurn(StopReason::EndTurn);
}

void AgentSession::runTools()
{
    Turn &turn = *m_turn;
    turn.results.resize(turn.calls.size());
    turn.pendingTools = static_cast<int>(turn.calls.size());
    const int generation = m_generation;

    for (qsizetype slot = 0; slot < turn.calls.size(); ++slot) {
        const ToolCallBlock call = turn.calls.at(slot);
        emit toolCallStarted(call.id, call.name, call.args);
        if (m_generation != generation)
            return; // a toolCallStarted slot ended the turn re-entrantly

        ITool *const tool = m_tools.value(call.name, nullptr);
        if (!tool) {
            // Unknown name: re-injected as an isError result so the model
            // can recover (counts toward the iteration cap). Deferred so
            // outcomes never complete inside this loop.
            QTimer::singleShot(0, this, [this, generation, slot, name = call.name] {
                if (m_generation != generation)
                    return;
                onToolOutcome(slot,
                              ToolOutcome{{TextBlock{QStringLiteral("unknown tool: ") + name}},
                                          true});
            });
            continue;
        }

        // A watcher per call: delivery is via the event loop even for
        // ready futures, and an abandoned turn (timeout/cancel) is just a
        // stale generation — the tool's future is never cancelled from here.
        auto *watcher = new QFutureWatcher<ToolOutcome>(this);
        connect(watcher, &QFutureWatcherBase::finished, this,
                [this, watcher, generation, slot] {
                    watcher->deleteLater();
                    if (m_generation != generation)
                        return;
                    const ToolOutcome outcome =
                        watcher->future().resultCount() > 0
                            ? watcher->result()
                            : ToolOutcome{{TextBlock{QStringLiteral(
                                              "tool finished without an outcome")}},
                                          true}; // contract violation, surfaced soft
                    onToolOutcome(slot, outcome);
                });
        watcher->setFuture(tool->invoke(call.args));
        if (m_generation != generation)
            return; // belt-and-braces against re-entrant invoke()
    }
}

void AgentSession::onToolOutcome(qsizetype slot, const ToolOutcome &outcome)
{
    Turn &turn = *m_turn;
    turn.results[slot] = ToolResultBlock{turn.calls.at(slot).id, outcome.parts, outcome.isError};
    if (--turn.pendingTools > 0)
        return; // the iteration waits for every call

    // All outcomes in: one Role::Tool message, results in original call
    // order regardless of resolution order (the codec fans out one wire
    // message per block).
    Message toolMessage{Role::Tool, {}};
    const int generation = m_generation;
    for (const ToolResultBlock &result : std::as_const(turn.results)) {
        emit toolCallFinished(result.callId, result);
        if (m_generation != generation)
            return; // a toolCallFinished slot ended the turn re-entrantly
        toolMessage.blocks.append(result);
    }
    m_history.append(std::move(toolMessage));
    startIteration();
}

void AgentSession::finishTurn(StopReason reason)
{
    if (!m_turn)
        return;
    closeTurn();
    emit busyChanged(false);
    emit turnFinished(reason); // terminal strictly last
}

void AgentSession::failTurn(AgentError error, bool cancelProvider)
{
    if (!m_turn)
        return;
    const qsizetype baseline = m_turn->historyBaseline;
    closeTurn();
    if (cancelProvider) // after closeTurn: the late ErrorEvent lands nowhere
        m_provider.cancel();
    m_history.resize(baseline); // only complete successful turns persist
    emit busyChanged(false);
    emit turnFailed(error); // terminal strictly last
}

void AgentSession::closeTurn()
{
    ++m_generation;
    dropWatcher();
    m_turnTimer.stop();
    m_turn.reset();
}

void AgentSession::dropWatcher()
{
    if (!m_turn || !m_turn->watcher)
        return;
    // May run inside the watcher's own resultReadyAt — release + deleteLater,
    // never a direct delete of an emitting sender.
    QFutureWatcher<StreamEvent> *watcher = m_turn->watcher.release();
    watcher->disconnect(this);
    watcher->deleteLater();
}

InferenceRequest AgentSession::buildRequest() const
{
    InferenceRequest request;
    request.model = m_modelConfig.model;
    request.reasoningEffort = m_modelConfig.reasoningEffort;
    request.temperature = m_modelConfig.temperature;
    request.seed = m_modelConfig.seed;
    request.maxTokens = m_modelConfig.maxTokens;
    request.tools = m_toolSpecs;
    // The system prompt is a lifetime invariant and the tools are fixed, so the
    // leading system+tools is a stable prefix reused on every iteration and turn
    // (always past the cache break-even). Dialects that can mark it, do.
    request.cacheStablePrefix = true;
    if (!m_systemPrompt.isEmpty())
        request.messages.append(Message{Role::System, {TextBlock{m_systemPrompt}}});
    request.messages.append(m_history);
    // Inject ambient at the tail: prepend it to the latest user message of the
    // OUTGOING copy only (m_history stays bare, so prior turns remain a stable
    // cacheable prefix). Within a turn's tool loop the latest user message is
    // the originating one, so ambient stays put across iterations.
    if (!m_ambient.isEmpty()) {
        for (qsizetype i = request.messages.size() - 1; i >= 0; --i) {
            if (request.messages[i].role == Role::User) {
                request.messages[i].blocks.prepend(TextBlock{m_ambient});
                break;
            }
        }
    }
    return request;
}

} // namespace karness
