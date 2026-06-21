// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "anthropicprovider.h"
#include "karnessfixtures.h"
#include "mockhttpserver.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDeadlineTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

using namespace karness;

namespace {

InferenceRequest simpleRequest()
{
    InferenceRequest request;
    request.model = QStringLiteral("claude-test");
    request.messages = {Message{Role::User, {TextBlock{QStringLiteral("hi")}}}};
    return request;
}

bool waitFinished(const QFuture<StreamEvent> &future, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!future.isFinished() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return future.isFinished();
}

bool waitForResults(const QFuture<StreamEvent> &future, int count, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (future.resultCount() < count && !future.isFinished() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return future.resultCount() >= count;
}

const ErrorEvent *terminalError(const QList<StreamEvent> &events)
{
    if (events.isEmpty())
        return nullptr;
    for (qsizetype i = 0; i < events.size() - 1; ++i)
        if (std::holds_alternative<ErrorEvent>(events.at(i))
            || std::holds_alternative<Done>(events.at(i)))
            return nullptr;
    return std::get_if<ErrorEvent>(&events.constLast());
}

const Done *terminalDone(const QList<StreamEvent> &events)
{
    if (events.isEmpty())
        return nullptr;
    for (qsizetype i = 0; i < events.size() - 1; ++i)
        if (std::holds_alternative<ErrorEvent>(events.at(i))
            || std::holds_alternative<Done>(events.at(i)))
            return nullptr;
    return std::get_if<Done>(&events.constLast());
}

} // namespace

class TestAnthropicProvider : public QObject {
    Q_OBJECT
private:
    MockHttpServer m_server;

    ProviderConfig config()
    {
        ProviderConfig cfg;
        cfg.baseUrl = m_server.baseUrl();
        return cfg;
    }

    QList<StreamEvent> runScript(MockHttpServer::Script script,
                                 const InferenceRequest &request = simpleRequest())
    {
        m_server.setScript(std::move(script));
        AnthropicProvider provider(config());
        auto future = provider.generate(request);
        if (!waitFinished(future))
            return {};
        return future.results();
    }

    static MockHttpServer::Script sseScript(const QByteArray &stream)
    {
        MockHttpServer::Script script;
        constexpr qsizetype width = 23; // hostile chunking across line / UTF-8 cuts
        for (qsizetype at = 0; at < stream.size(); at += width)
            script.chunks.append(stream.mid(at, width));
        return script;
    }

private slots:
    void initTestCase() { QVERIFY(m_server.start()); }

    void happyTextStream()
    {
        const auto events = runScript(sseScript(fixtures::anthropicTextStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::EndTurn);
        QVERIFY(done->usage.has_value());
        QCOMPARE(done->usage->inputTokens, 14);  // from message_start
        QCOMPARE(done->usage->outputTokens, 27); // merged from message_delta
        QCOMPARE(done->message.role, Role::Assistant);
        QCOMPARE(done->message.blocks.size(), 1);
        QCOMPARE(std::get<TextBlock>(done->message.blocks.first()).text,
                 QStringLiteral("Your basil needs water."));
    }

    void toolUseAssembled()
    {
        const auto events = runScript(sseScript(fixtures::anthropicToolUseStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::ToolCalls);
        QCOMPARE(done->message.blocks.size(), 1);
        const auto &call = std::get<ToolCallBlock>(done->message.blocks.first());
        QCOMPARE(call.id, QStringLiteral("toolu_7"));
        QCOMPARE(call.name, QStringLiteral("read_plant_data"));
        QCOMPARE(call.args.value(QStringLiteral("plantId")).toString(), QStringLiteral("p-1"));
    }

    void thinkingBecomesReasoningWithSignature()
    {
        const auto events = runScript(sseScript(fixtures::anthropicThinkingStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->message.blocks.size(), 2);
        const auto &reasoning = std::get<ReasoningBlock>(done->message.blocks.at(0));
        QCOMPARE(reasoning.text, QStringLiteral("moisture is below the floor"));
        // The opaque signature must survive into the assembled message (echoed in a tool loop).
        QCOMPARE(reasoning.providerOpaque.value(QStringLiteral("signature")).toString(),
                 QStringLiteral("sig-abc123"));
        QCOMPARE(std::get<TextBlock>(done->message.blocks.at(1)).text,
                 QStringLiteral("Water the basil."));
    }

    void requestWireShape()
    {
        InferenceRequest request = simpleRequest();
        request.messages.prepend(Message{Role::System, {TextBlock{QStringLiteral("You are a gardener.")}}});
        request.tools = {ToolSpec{QStringLiteral("list_plants"), QStringLiteral("d"), QJsonObject()}};
        request.reasoningEffort = ReasoningEffort::Medium; // -> thinking budget 4096
        m_server.setScript(sseScript(fixtures::anthropicTextStream()));
        ProviderConfig cfg = config();
        cfg.apiKey = QStringLiteral("sk-ant-test");
        AnthropicProvider provider(cfg);
        auto future = provider.generate(request);
        QVERIFY(waitFinished(future));

        // Qt canonicalizes raw header names to Train-Case (X-Api-Key, …) — match case-insensitively.
        const QByteArray headers = m_server.lastRequestHeaders().toLower();
        QVERIFY(headers.startsWith("post /v1/messages http/1.1"));
        QVERIFY(headers.contains("x-api-key: sk-ant-test"));
        QVERIFY(headers.contains("anthropic-version: 2023-06-01"));

        const QJsonObject body = QJsonDocument::fromJson(m_server.lastRequestBody()).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("claude-test"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);
        // System is hoisted to the top level, not a message.
        QCOMPARE(body.value(QStringLiteral("system")).toString(), QStringLiteral("You are a gardener."));
        QCOMPARE(body.value(QStringLiteral("messages")).toArray().size(), 1);
        // Tools use input_schema, not the OpenAI nesting.
        const QJsonObject tool = body.value(QStringLiteral("tools")).toArray().first().toObject();
        QCOMPARE(tool.value(QStringLiteral("name")).toString(), QStringLiteral("list_plants"));
        QVERIFY(tool.contains(QStringLiteral("input_schema")));
        // Thinking enabled with the mapped budget; max_tokens must exceed it.
        const QJsonObject thinking = body.value(QStringLiteral("thinking")).toObject();
        QCOMPARE(thinking.value(QStringLiteral("type")).toString(), QStringLiteral("enabled"));
        QCOMPARE(thinking.value(QStringLiteral("budget_tokens")).toInt(), 4096);
        QVERIFY(body.value(QStringLiteral("max_tokens")).toInt() > 4096);
    }

    void cacheStablePrefixAddsEphemeralBreakpoint()
    {
        InferenceRequest request = simpleRequest();
        request.messages.prepend(Message{Role::System, {TextBlock{QStringLiteral("Stable prompt.")}}});
        request.tools = {ToolSpec{QStringLiteral("list_plants"), QStringLiteral("d"), QJsonObject()}};
        request.cacheStablePrefix = true;
        m_server.setScript(sseScript(fixtures::anthropicTextStream()));
        ProviderConfig cfg = config();
        cfg.apiKey = QStringLiteral("sk-ant-test");
        AnthropicProvider provider(cfg);
        auto future = provider.generate(request);
        QVERIFY(waitFinished(future));

        const QJsonObject body = QJsonDocument::fromJson(m_server.lastRequestBody()).object();
        // System becomes a content-block array carrying the cache breakpoint (caches
        // tools + system together — render order is tools -> system -> messages).
        const QJsonArray system = body.value(QStringLiteral("system")).toArray();
        QCOMPARE(system.size(), 1);
        const QJsonObject block = system.first().toObject();
        QCOMPARE(block.value(QStringLiteral("text")).toString(), QStringLiteral("Stable prompt."));
        QCOMPARE(block.value(QStringLiteral("cache_control")).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("ephemeral"));
        // The tool stays unmarked — the system breakpoint already covers it.
        QVERIFY(!body.value(QStringLiteral("tools")).toArray().first().toObject().contains(
            QStringLiteral("cache_control")));
    }

    void errorEventSurfacesAsProviderError()
    {
        const auto events = runScript(sseScript(fixtures::anthropicErrorEventStream()));
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Provider);
        QCOMPARE(error->error.message, QStringLiteral("Overloaded"));
    }

    void http401SurfacesAnthropicErrorMessage()
    {
        MockHttpServer::Script script;
        script.status = 401;
        script.statusText = "Unauthorized";
        script.contentType = "application/json";
        script.chunks = {fixtures::anthropicErrorBody401()};
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Http);
        QCOMPARE(error->error.httpStatus, 401);
        QCOMPARE(error->error.message, QStringLiteral("invalid x-api-key"));
    }

    void connectionDropMidStreamIsNetworkError()
    {
        MockHttpServer::Script script = sseScript(fixtures::anthropicPartialStream());
        script.dropAfterChunks = true;
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Network);
    }

    void cancelMidStream()
    {
        MockHttpServer::Script script = sseScript(fixtures::anthropicPartialStream());
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        AnthropicProvider provider(config());
        auto future = provider.generate(simpleRequest());
        QVERIFY(waitForResults(future, 1));
        provider.cancel();
        QVERIFY(waitFinished(future));
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Cancelled);
    }

    void stallTimeoutMapsToTimeout()
    {
        MockHttpServer::Script script = sseScript(fixtures::anthropicPartialStream());
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        ProviderConfig cfg = config();
        cfg.stallTimeout = std::chrono::milliseconds(300);
        AnthropicProvider provider(cfg);
        auto future = provider.generate(simpleRequest());
        QVERIFY(waitFinished(future));
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Timeout);
    }

    void imageRequestEncodesAndStreams()
    {
        // A user image encodes as a base64 image source block and the request streams to done.
        InferenceRequest request = simpleRequest();
        request.messages.first().blocks.append(
            ImageBlock{QByteArrayLiteral("png"), QStringLiteral("image/png")});
        const auto events = runScript(sseScript(fixtures::anthropicTextStream()), request);
        QVERIFY(terminalDone(events));
        const QByteArray body = m_server.lastRequestBody();
        QVERIFY(body.contains(QByteArrayLiteral("\"type\":\"image\"")));
        QVERIFY(body.contains(QByteArrayLiteral("base64")));
        QVERIFY(body.contains(QByteArrayLiteral("png").toBase64()));
    }
};

QTEST_GUILESS_MAIN(TestAnthropicProvider)
#include "test_anthropicprovider.moc"
