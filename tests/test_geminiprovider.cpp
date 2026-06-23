// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "geminiprovider.h"
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
    request.model = QStringLiteral("gemini-test");
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

class TestGeminiProvider : public QObject {
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
        GeminiProvider provider(config());
        auto future = provider.generate(request);
        if (!waitFinished(future))
            return {};
        return future.results();
    }

    static MockHttpServer::Script sseScript(const QByteArray &stream)
    {
        MockHttpServer::Script script;
        constexpr qsizetype width = 23;
        for (qsizetype at = 0; at < stream.size(); at += width)
            script.chunks.append(stream.mid(at, width));
        return script;
    }

private slots:
    void initTestCase() { QVERIFY(m_server.start()); }

    void happyTextStream()
    {
        const auto events = runScript(sseScript(fixtures::geminiTextStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::EndTurn);
        QVERIFY(done->usage.has_value());
        QCOMPARE(done->usage->inputTokens, 9);
        QCOMPARE(done->usage->outputTokens, 18);
        QCOMPARE(done->message.blocks.size(), 1);
        QCOMPARE(std::get<TextBlock>(done->message.blocks.first()).text,
                 QStringLiteral("Your basil needs water."));
    }

    void toolCallAssembled()
    {
        const auto events = runScript(sseScript(fixtures::geminiToolCallStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::ToolCalls);
        const auto &call = std::get<ToolCallBlock>(done->message.blocks.first());
        QCOMPARE(call.name, QStringLiteral("read_plant_data"));
        QCOMPARE(call.id, QStringLiteral("read_plant_data")); // synthesized from the name
        QCOMPARE(call.args.value(QStringLiteral("plantId")).toString(), QStringLiteral("p-1"));
    }

    void thoughtBecomesReasoningWithSignature()
    {
        const auto events = runScript(sseScript(fixtures::geminiThoughtStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->message.blocks.size(), 2);
        const auto &reasoning = std::get<ReasoningBlock>(done->message.blocks.at(0));
        QCOMPARE(reasoning.text, QStringLiteral("considering moisture"));
        QCOMPARE(reasoning.providerOpaque.value(QStringLiteral("thoughtSignature")).toString(),
                 QStringLiteral("tsig-1"));
        QCOMPARE(std::get<TextBlock>(done->message.blocks.at(1)).text, QStringLiteral("Water it."));
    }

    void requestWireShape()
    {
        InferenceRequest request = simpleRequest();
        request.messages.prepend(Message{Role::System, {TextBlock{QStringLiteral("You are a gardener.")}}});
        request.tools = {ToolSpec{QStringLiteral("list_plants"), QStringLiteral("d"), QJsonObject()}};
        request.reasoningEffort = ReasoningEffort::Low; // -> thinkingBudget 1024
        m_server.setScript(sseScript(fixtures::geminiTextStream()));
        ProviderConfig cfg = config();
        cfg.apiKey = QStringLiteral("goog-key");
        GeminiProvider provider(cfg);
        auto future = provider.generate(request);
        QVERIFY(waitFinished(future));

        const QByteArray headers = m_server.lastRequestHeaders().toLower();
        QVERIFY(headers.contains("post /v1/models/gemini-test:streamgeneratecontent?alt=sse"));
        QVERIFY(headers.contains("x-goog-api-key: goog-key"));

        const QJsonObject body = QJsonDocument::fromJson(m_server.lastRequestBody()).object();
        // System -> systemInstruction; contents has the single user turn.
        QCOMPARE(body.value(QStringLiteral("systemInstruction"))
                     .toObject()
                     .value(QStringLiteral("parts"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("text"))
                     .toString(),
                 QStringLiteral("You are a gardener."));
        QCOMPARE(body.value(QStringLiteral("contents")).toArray().size(), 1);
        // Tools wrapped in functionDeclarations.
        const QJsonObject decl = body.value(QStringLiteral("tools"))
                                     .toArray()
                                     .first()
                                     .toObject()
                                     .value(QStringLiteral("functionDeclarations"))
                                     .toArray()
                                     .first()
                                     .toObject();
        QCOMPARE(decl.value(QStringLiteral("name")).toString(), QStringLiteral("list_plants"));
        // Thinking budget mapped.
        QCOMPARE(body.value(QStringLiteral("generationConfig"))
                     .toObject()
                     .value(QStringLiteral("thinkingConfig"))
                     .toObject()
                     .value(QStringLiteral("thinkingBudget"))
                     .toInt(),
                 1024);
    }

    void toolSchemaIsSanitizedForGemini()
    {
        // Gemini's functionDeclarations parser rejects JSON-Schema keywords it doesn't know
        // (additionalProperties, $schema, …) with an "Invalid JSON payload / Unknown name"
        // error — while OpenAI/Anthropic accept the same schema. The shared tool definitions
        // stamp `additionalProperties: false` onto every schema, so the Gemini codec must strip
        // it (top-level and nested) or every tool-enabled Gemini turn fails.
        InferenceRequest request = simpleRequest();
        QJsonObject nested{{QStringLiteral("type"), QStringLiteral("object")},
                           {QStringLiteral("properties"),
                            QJsonObject{{QStringLiteral("n"),
                                         QJsonObject{{QStringLiteral("type"),
                                                      QStringLiteral("integer")}}}}},
                           {QStringLiteral("additionalProperties"), false}};
        QJsonObject schema{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("$schema"), QStringLiteral("https://json-schema.org/draft/2020-12")},
            {QStringLiteral("properties"),
             QJsonObject{{QStringLiteral("q"),
                          QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                         {QStringLiteral("opts"), nested}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("q")}},
            {QStringLiteral("additionalProperties"), false}};
        request.tools = {ToolSpec{QStringLiteral("list_plants"), QStringLiteral("d"), schema}};
        m_server.setScript(sseScript(fixtures::geminiTextStream()));
        GeminiProvider provider(config());
        auto future = provider.generate(request);
        QVERIFY(waitFinished(future));

        const QJsonObject params = QJsonDocument::fromJson(m_server.lastRequestBody())
                                       .object()
                                       .value(QStringLiteral("tools"))
                                       .toArray()
                                       .first()
                                       .toObject()
                                       .value(QStringLiteral("functionDeclarations"))
                                       .toArray()
                                       .first()
                                       .toObject()
                                       .value(QStringLiteral("parameters"))
                                       .toObject();
        // Unsupported keywords gone, top-level and nested; the supported shape is preserved.
        QVERIFY(!params.contains(QStringLiteral("additionalProperties")));
        QVERIFY(!params.contains(QStringLiteral("$schema")));
        QCOMPARE(params.value(QStringLiteral("type")).toString(), QStringLiteral("object"));
        QVERIFY(params.value(QStringLiteral("required")).toArray().contains(QStringLiteral("q")));
        const QJsonObject opts = params.value(QStringLiteral("properties"))
                                     .toObject()
                                     .value(QStringLiteral("opts"))
                                     .toObject();
        QVERIFY(!opts.contains(QStringLiteral("additionalProperties")));
        QVERIFY(opts.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("n")));
    }

    void errorChunkSurfacesAsProviderError()
    {
        const auto events = runScript(sseScript(fixtures::geminiErrorChunkStream()));
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Provider);
        QCOMPARE(error->error.message, QStringLiteral("overloaded"));
    }

    void http401SurfacesErrorMessage()
    {
        MockHttpServer::Script script;
        script.status = 401;
        script.statusText = "Unauthorized";
        script.contentType = "application/json";
        script.chunks = {fixtures::geminiErrorBody401()};
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Http);
        QCOMPARE(error->error.httpStatus, 401);
        QCOMPARE(error->error.message, QStringLiteral("API key not valid"));
    }

    void connectionDropMidStreamIsNetworkError()
    {
        MockHttpServer::Script script = sseScript(fixtures::geminiPartialStream());
        script.dropAfterChunks = true;
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Network);
    }

    void cancelMidStream()
    {
        MockHttpServer::Script script = sseScript(fixtures::geminiPartialStream());
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        GeminiProvider provider(config());
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
        MockHttpServer::Script script = sseScript(fixtures::geminiPartialStream());
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        ProviderConfig cfg = config();
        cfg.stallTimeout = std::chrono::milliseconds(300);
        GeminiProvider provider(cfg);
        auto future = provider.generate(simpleRequest());
        QVERIFY(waitFinished(future));
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Timeout);
    }

    void imageRequestEncodesAndStreams()
    {
        // A user image encodes as an inlineData part and the request streams to done.
        InferenceRequest request = simpleRequest();
        request.messages.first().blocks.append(
            ImageBlock{QByteArrayLiteral("png"), QStringLiteral("image/png")});
        const auto events = runScript(sseScript(fixtures::geminiTextStream()), request);
        QVERIFY(terminalDone(events));
        const QByteArray body = m_server.lastRequestBody();
        QVERIFY(body.contains(QByteArrayLiteral("inlineData")));
        QVERIFY(body.contains(QByteArrayLiteral("png").toBase64()));
    }
};

QTEST_GUILESS_MAIN(TestGeminiProvider)
#include "test_geminiprovider.moc"
