// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iprovider.h"
#include "itool.h"

#include <QtCore/QFutureWatcher>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QTimer>

#include <chrono>
#include <expected>
#include <memory>

namespace karness {

struct AgentSessionConfig {
    // Provider calls allowed per user turn before the loop fails with
    // Code::LoopLimit (docs/adr/0019 decision 8 — the deep-dive's
    // non-negotiable guard; never a spin).
    int maxIterations = 8;
    // Whole-turn wall clock: ALL iterations + tool time. The provider's
    // stallTimeout only guards a silent transport; this is the budget for
    // a model that streams healthily forever or a tool that never resolves.
    // Covers tool waits — a user-confirmation tool must budget for it
    // or the host disables it. 0 disables.
    std::chrono::milliseconds turnTimeout = std::chrono::minutes(5);
};

// Owns one conversation: the bounded tool loop over an IProvider
// (docs/adr/0019 decision 8). History contains only complete successful
// turns — a failed turn rolls back to the pre-send snapshot so a retry is
// clean (a dangling ToolCall without its ToolResult is invalid wire history
// on every dialect).
//
// Terminal contract: each accepted send() ends with exactly ONE of
// turnFinished XOR turnFailed, emitted strictly last, after which busy()
// is false. Deltas feed the signals for live UI only; what enters history
// is Done's assembled message verbatim (preserving ReasoningBlock's
// providerOpaque through the loop — decision 2).
class AgentSession : public QObject {
    Q_OBJECT
public:
    // The system prompt is a LIFETIME INVARIANT (docs/adr/0019 cache-placement
    // follow-up): it is fixed at construction, emitted as the leading
    // Role::System message on every request, and never stored in history().
    // There is deliberately no setter — mutating a session's constitution
    // mid-conversation is incoherent and is exactly what would let volatile
    // text leak into the cacheable prefix. "New prompt ⇒ new session." An
    // empty prompt emits no System message.
    explicit AgentSession(IProvider &provider, QString systemPrompt = {},
                          AgentSessionConfig config = {}, QObject *parent = nullptr);
    ~AgentSession() override;

    // Non-owning; the host keeps tools alive for the session's lifetime.
    // Keyed by spec().name; advertisement order = registration order.
    void setTools(const QList<ITool *> &tools);
    // Per-turn ambient context (docs/adr/0019 cache-placement follow-up): set
    // once, re-read on every request, refreshed by the caller when it changes,
    // and NEVER stored in history(). Injected at the TAIL — prepended to the
    // latest user message of the outgoing request — so the system prompt +
    // tools + prior history stay a stable cacheable prefix and only the
    // current turn carries the fresh ambient.
    void setAmbient(QString ambient);
    // Model/sampling/reasoning knobs. The session owns messages and tools and
    // assembles the full InferenceRequest itself, so only the knobs are set here.
    void setModelConfig(ModelConfig config);

    // Synchronous preconditions as values: a turn already in flight ->
    // Code::Provider, provider !isReady() -> Code::NotReady. Everything
    // after launch is signal-surfaced.
    [[nodiscard]] std::expected<void, AgentError> send(const QString &userText);
    [[nodiscard]] std::expected<void, AgentError> send(Message userMessage);
    // No-op when idle. Fail-first: the turn ends with turnFailed(Cancelled)
    // BEFORE the provider is cancelled, so its late ErrorEvent lands on the
    // detached watcher and a transport-level Cancelled never masks the real
    // terminal. Pending tool futures are abandoned, not cancelled.
    void cancel();

    [[nodiscard]] bool busy() const { return m_turn != nullptr; }
    [[nodiscard]] const QList<Message> &history() const { return m_history; }

signals:
    void busyChanged(bool busy);
    void textDelta(const QString &text);      // live UI only; empty deltas dropped
    void reasoningDelta(const QString &text); // live UI only; empty deltas dropped
    void toolCallStarted(const QString &callId, const QString &name, const QJsonObject &args);
    // Emitted once the iteration's calls are all in, in original call order.
    void toolCallFinished(const QString &callId, const karness::ToolResultBlock &result);
    // MaxTokens/ContentFilter/Other finish successfully with the partial
    // message kept — the reason is the UI's truncation notice.
    void turnFinished(karness::StopReason stopReason);
    void turnFailed(const karness::AgentError &error);

private:
    struct Turn {
        int iterations = 0;            // provider calls made this turn
        qsizetype historyBaseline = 0; // rollback point on failure
        std::unique_ptr<QFutureWatcher<StreamEvent>> watcher;
        QList<ToolCallBlock> calls;     // current iteration's calls, in order
        QList<ToolResultBlock> results; // slot-indexed to match calls
        int pendingTools = 0;
    };

    void startIteration();
    void onStreamEvent(const StreamEvent &event);
    void handleDone(const Done &done);
    void runTools();
    void onToolOutcome(qsizetype slot, const ToolOutcome &outcome);
    void onTurnTimeout();
    void finishTurn(StopReason reason);
    void failTurn(AgentError error, bool cancelProvider = false);
    void closeTurn(); // generation bump + watcher detach + turn reset
    void dropWatcher();
    [[nodiscard]] InferenceRequest buildRequest() const;

    IProvider &m_provider;
    AgentSessionConfig m_config;
    QHash<QString, ITool *> m_tools; // non-owning, keyed by spec().name
    QList<ToolSpec> m_toolSpecs;     // advertised on every request
    const QString m_systemPrompt;    // lifetime invariant — no setter (see ctor)
    QString m_ambient;               // per-turn tail context; never enters history()
    ModelConfig m_modelConfig;       // generation knobs copied onto each request
    QList<Message> m_history;
    QTimer m_turnTimer; // single-shot whole-turn budget, armed at send()
    std::unique_ptr<Turn> m_turn;
    int m_generation = 0; // bumped at every terminal; stale async work ignored
};

} // namespace karness
