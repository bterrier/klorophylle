// SPDX-License-Identifier: GPL-3.0-or-later
#include "streamingprovider.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace karness {

StreamingProvider::StreamingProvider(ProviderConfig config, std::unique_ptr<Dialect> dialect,
                                     QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_dialect(std::move(dialect))
{
}

StreamingProvider::~StreamingProvider()
{
    m_cancelRequested = true;
    finishWithError(AgentError{AgentError::Code::Cancelled,
                               QStringLiteral("provider destroyed mid-turn"), {}});
}

ModelCaps StreamingProvider::caps() const
{
    return m_config.caps;
}

bool StreamingProvider::isReady() const
{
    return m_config.baseUrl.isValid() && !m_config.baseUrl.isEmpty();
}

QFuture<StreamEvent> StreamingProvider::generate(const InferenceRequest &request)
{
    if (m_turn) { // misuse is loud; the AgentSession serializes turns
        QPromise<StreamEvent> rejected;
        QFuture<StreamEvent> future = rejected.future();
        rejected.start();
        rejected.addResult(ErrorEvent{AgentError{AgentError::Code::Provider,
                                                 QStringLiteral("a turn is already in flight"), {}}});
        rejected.finish();
        return future;
    }

    m_turn = std::make_unique<Turn>();
    m_turn->promise.start();
    QFuture<StreamEvent> future = m_turn->promise.future();

    const auto body = m_dialect->encodeRequest(request);
    if (!body) {
        finishWithError(body.error()); // no network touched
        return future;
    }

    m_cancelRequested = false;
    QNetworkRequest netRequest(m_dialect->endpoint(m_config.baseUrl, request));
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    netRequest.setRawHeader("Accept", "text/event-stream");
    m_dialect->applyAuth(netRequest, m_config.apiKey);
    if (m_config.stallTimeout.count() > 0)
        netRequest.setTransferTimeout(m_config.stallTimeout);

    m_reply = m_network.post(netRequest, QJsonDocument(*body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QIODevice::readyRead, this, &StreamingProvider::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &StreamingProvider::onFinished);
    return future;
}

void StreamingProvider::cancel()
{
    if (!m_turn)
        return;
    m_cancelRequested = true;
    if (m_reply)
        m_reply->abort(); // finished() fires and maps to Code::Cancelled
}

void StreamingProvider::onReadyRead()
{
    if (!m_turn || !m_reply)
        return;
    if (m_turn->httpStatus == 0)
        m_turn->httpStatus =
            m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    const QByteArray bytes = m_reply->readAll();
    if (m_turn->httpStatus < 200 || m_turn->httpStatus >= 300) {
        m_turn->errorBody += bytes; // surfaced at finished()
        return;
    }

    const QList<ServerSentEvent> events = m_turn->sse.feed(bytes);
    const bool think = m_dialect->extractsThinkTags();
    for (const ServerSentEvent &event : events) {
        if (!m_turn || m_turn->terminalEmitted)
            return;
        if (m_dialect->isTerminalSentinel(event)) {
            finishWithDone();
            return;
        }
        const auto chunk = m_dialect->decodeEvent(event);
        if (!chunk) {
            finishWithError(chunk.error());
            return;
        }
        if (chunk->stopReason)
            m_turn->stopReason = chunk->stopReason;
        if (chunk->usage) { // merge: some dialects split input/output across frames
            if (!m_turn->usage)
                m_turn->usage = TokenUsage{};
            if (chunk->usage->inputTokens)
                m_turn->usage->inputTokens = chunk->usage->inputTokens;
            if (chunk->usage->outputTokens)
                m_turn->usage->outputTokens = chunk->usage->outputTokens;
        }
        for (const StreamEvent &decoded : chunk->events)
            emitEvents(think ? m_turn->think.feed(decoded) : QList<StreamEvent>{decoded});
    }
}

void StreamingProvider::onFinished()
{
    if (!m_turn || !m_reply)
        return;
    if (m_reply->isOpen())
        onReadyRead(); // drain bytes buffered after the last readyRead
    if (!m_turn)       // draining reached a terminal ([DONE], decode error…)
        return;

    if (m_turn->httpStatus == 0) // error replies may never signal readyRead
        m_turn->httpStatus =
            m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (m_turn->httpStatus != 0 && (m_turn->httpStatus < 200 || m_turn->httpStatus >= 300)) {
        finishWithError(httpError(m_turn->httpStatus, m_turn->errorBody));
        return;
    }

    // A transfer-timeout abort surfaces as TimeoutError on this Qt but is
    // documented as OperationCanceledError — accept both spellings, and let
    // the m_cancelRequested flag claim cancellation either way.
    const QNetworkReply::NetworkError error = m_reply->error();
    if (error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError) {
        finishWithError(m_cancelRequested
                            ? AgentError{AgentError::Code::Cancelled,
                                         QStringLiteral("turn cancelled"), {}}
                            : AgentError{AgentError::Code::Timeout,
                                         QStringLiteral("stream stalled past the transport timeout"),
                                         {}});
        return;
    }
    if (error != QNetworkReply::NoError) {
        finishWithError(AgentError{AgentError::Code::Network, m_reply->errorString(), {}});
        return;
    }

    // Clean end without a sentinel: lenient when a stop reason proved the turn
    // completed (event-typed dialects never send "[DONE]", and some proxies
    // drop it), a protocol violation otherwise.
    if (m_turn->stopReason)
        finishWithDone();
    else
        finishWithError(AgentError{AgentError::Code::Parse,
                                   QStringLiteral("stream ended without completion"), {}});
}

void StreamingProvider::emitEvents(const QList<StreamEvent> &events)
{
    for (const StreamEvent &event : events) {
        m_turn->accumulator.feed(event);
        m_turn->promise.addResult(event);
    }
}

void StreamingProvider::finishWithDone()
{
    if (!m_turn || m_turn->terminalEmitted)
        return;
    if (m_dialect->extractsThinkTags())
        emitEvents(m_turn->think.flush()); // residual withheld text/reasoning

    auto turn = std::move(m_turn); // single-terminal guard: m_turn gone first
    teardownTransport();
    turn->terminalEmitted = true;

    const auto message = turn->accumulator.finish();
    if (!message) // malformed accumulated tool args -> Parse terminal
        turn->promise.addResult(ErrorEvent{message.error()});
    else
        turn->promise.addResult(
            Done{*message, turn->stopReason.value_or(StopReason::EndTurn), turn->usage});
    turn->promise.finish();
}

void StreamingProvider::finishWithError(AgentError error)
{
    if (!m_turn || m_turn->terminalEmitted)
        return;
    auto turn = std::move(m_turn);
    teardownTransport();
    turn->terminalEmitted = true;
    turn->promise.addResult(ErrorEvent{std::move(error)});
    turn->promise.finish();
}

void StreamingProvider::teardownTransport()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this); // no re-entry from abort()'s finished()
    reply->abort();
    reply->deleteLater();
}

AgentError StreamingProvider::httpError(int status, const QByteArray &body)
{
    QString message;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonValue error = doc.object().value(QStringLiteral("error"));
        if (error.isObject()) // OpenAI: {"error":{"message":...}}; Gemini: {"error":{"message":...}}
            message = error.toObject().value(QStringLiteral("message")).toString();
        else if (error.isString()) // Ollama: {"error":"..."}
            message = error.toString();
    }
    if (message.isEmpty()) // non-JSON body: keep a short raw snippet
        message = QString::fromUtf8(body.first(std::min<qsizetype>(body.size(), 200)));
    if (message.isEmpty())
        message = QStringLiteral("HTTP error");
    return AgentError{AgentError::Code::Http, message, status};
}

} // namespace karness
