// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "karnessfixtures.h"
#include "mockhttpserver.h"
#include "openaicompatprovider.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDeadlineTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

using namespace karness;

namespace {

InferenceRequest simpleRequest()
{
    InferenceRequest request;
    request.model = QStringLiteral("test-model");
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

// The terminal must be the LAST event and unique — assert while extracting.
const ErrorEvent *terminalError(const QList<StreamEvent> &events)
{
    if (events.isEmpty())
        return nullptr;
    for (qsizetype i = 0; i < events.size() - 1; ++i)
        if (std::holds_alternative<ErrorEvent>(events.at(i))
            || std::holds_alternative<Done>(events.at(i)))
            return nullptr; // terminal not last / not unique
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

class TestOpenAiCompatProvider : public QObject {
    Q_OBJECT
private:
    MockHttpServer m_server;

    OpenAiCompatConfig config()
    {
        OpenAiCompatConfig cfg;
        cfg.baseUrl = m_server.baseUrl();
        return cfg;
    }

    QList<StreamEvent> runScript(MockHttpServer::Script script,
                                 const InferenceRequest &request = simpleRequest())
    {
        m_server.setScript(std::move(script));
        OpenAiCompatProvider provider(config());
        auto future = provider.generate(request);
        if (!waitFinished(future))
            return {}; // empty: every test asserts on content
        return future.results();
    }

    static MockHttpServer::Script sseScript(const QByteArray &stream)
    {
        MockHttpServer::Script script;
        // Hostile chunking: split the stream at a fixed width so events span
        // multiple readyRead slices (incl. mid-line and mid-UTF-8 cuts).
        constexpr qsizetype width = 23;
        for (qsizetype at = 0; at < stream.size(); at += width)
            script.chunks.append(stream.mid(at, width));
        return script;
    }

private slots:
    void initTestCase() { QVERIFY(m_server.start()); }

    void happyTextStream()
    {
        const auto events = runScript(sseScript(fixtures::openaiTextStream()));
        QCOMPARE(events.size(), 3);
        QCOMPARE(std::get<TextDelta>(events.at(0)).text, QStringLiteral("Your basil"));
        QCOMPARE(std::get<TextDelta>(events.at(1)).text, QStringLiteral(" needs water."));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::EndTurn);
        QVERIFY(done->usage.has_value());
        QCOMPARE(done->usage->inputTokens, 12);
        QCOMPARE(done->usage->outputTokens, 34);
        QCOMPARE(done->message.role, Role::Assistant);
        QCOMPARE(done->message.blocks.size(), 1);
        QCOMPARE(std::get<TextBlock>(done->message.blocks.first()).text,
                 QStringLiteral("Your basil needs water."));
    }

    void parallelToolCallsAssembled()
    {
        const auto events = runScript(sseScript(fixtures::openaiParallelToolCallsStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::ToolCalls);
        QCOMPARE(done->message.blocks.size(), 2);
        const auto &first = std::get<ToolCallBlock>(done->message.blocks.at(0));
        const auto &second = std::get<ToolCallBlock>(done->message.blocks.at(1));
        QCOMPARE(first.id, QStringLiteral("call_a"));
        QCOMPARE(first.name, QStringLiteral("list_plants"));
        QVERIFY(first.args.isEmpty());
        QCOMPARE(second.id, QStringLiteral("call_b"));
        QCOMPARE(second.name, QStringLiteral("read_plant_data"));
        QCOMPARE(second.args.value(QStringLiteral("plantId")).toString(), QStringLiteral("p-1"));
        // The streamed start events arrived before the args fragments.
        QCOMPARE(std::get<ToolCallStart>(events.at(0)).index, 0);
        QCOMPARE(std::get<ToolCallStart>(events.at(1)).index, 1);
    }

    void ollamaSingleChunkToolCall()
    {
        const auto events = runScript(sseScript(fixtures::ollamaToolCallStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::ToolCalls);
        QVERIFY(done->usage.has_value());
        QCOMPARE(done->usage->inputTokens, 210);
        const auto &call = std::get<ToolCallBlock>(done->message.blocks.first());
        QCOMPARE(call.id, QStringLiteral("call_x9"));
        QCOMPARE(call.args.value(QStringLiteral("plantId")).toString(), QStringLiteral("p-1"));
    }

    void llamaServerTimingsIgnored()
    {
        const auto events = runScript(sseScript(fixtures::llamaServerTextStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(std::get<TextBlock>(done->message.blocks.first()).text,
                 QStringLiteral("Soil is dry."));
        QVERIFY(done->usage.has_value());
        QCOMPARE(done->usage->outputTokens, 8);
    }

    void keepaliveCommentsSkipped()
    {
        const auto events = runScript(sseScript(fixtures::openrouterKeepaliveStream()));
        QCOMPARE(events.size(), 2);
        QCOMPARE(std::get<TextDelta>(events.first()).text, QStringLiteral("Hi"));
        QVERIFY(terminalDone(events));
    }

    void crlfStreamDecodesIdentically()
    {
        const auto events = runScript(sseScript(fixtures::crlfStream()));
        QCOMPARE(events.size(), 2);
        QCOMPARE(std::get<TextDelta>(events.first()).text, QStringLiteral("Hi"));
        QVERIFY(terminalDone(events));
    }

    void thinkTagsBecomeReasoning()
    {
        const auto events = runScript(sseScript(fixtures::qwenThinkTagStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->message.blocks.size(), 2);
        QCOMPARE(std::get<ReasoningBlock>(done->message.blocks.at(0)).text,
                 QStringLiteral("dry soil, low light"));
        QCOMPARE(std::get<TextBlock>(done->message.blocks.at(1)).text,
                 QStringLiteral("Move it and water."));
    }

    void reasoningContentFieldBecomesReasoning()
    {
        const auto events = runScript(sseScript(fixtures::deepseekReasoningStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->message.blocks.size(), 2);
        QCOMPARE(std::get<ReasoningBlock>(done->message.blocks.at(0)).text,
                 QStringLiteral("moisture 12% is below the 20% floor"));
        QCOMPARE(std::get<TextBlock>(done->message.blocks.at(1)).text,
                 QStringLiteral("Water the basil."));
    }

    void requestWireShape()
    {
        InferenceRequest request = simpleRequest();
        request.tools = {ToolSpec{QStringLiteral("list_plants"), QStringLiteral("d"), QJsonObject()}};
        m_server.setScript(sseScript(fixtures::openaiTextStream()));
        OpenAiCompatConfig cfg = config();
        cfg.apiKey = QStringLiteral("sk-test");
        OpenAiCompatProvider provider(cfg);
        auto future = provider.generate(request);
        QVERIFY(waitFinished(future));

        const QByteArray headers = m_server.lastRequestHeaders();
        QVERIFY(headers.startsWith("POST /v1/chat/completions HTTP/1.1"));
        QVERIFY(headers.contains("Authorization: Bearer sk-test"));
        QVERIFY(headers.contains("text/event-stream"));

        const QJsonObject body = QJsonDocument::fromJson(m_server.lastRequestBody()).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("test-model"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);
        QVERIFY(body.value(QStringLiteral("stream_options"))
                    .toObject()
                    .value(QStringLiteral("include_usage"))
                    .toBool());
        QVERIFY(body.contains(QStringLiteral("tools")));
        QVERIFY(!body.contains(QStringLiteral("temperature")));
    }

    void noApiKeyMeansNoAuthorizationHeader()
    {
        const auto events = runScript(sseScript(fixtures::openaiTextStream()));
        QVERIFY(terminalDone(events));
        QVERIFY(!m_server.lastRequestHeaders().contains("Authorization:"));
    }

    void http401SurfacesOpenAiErrorMessage()
    {
        MockHttpServer::Script script;
        script.status = 401;
        script.statusText = "Unauthorized";
        script.contentType = "application/json";
        script.chunks = {fixtures::openaiErrorBody401()};
        const auto events = runScript(script);
        QCOMPARE(events.size(), 1);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Http);
        QCOMPARE(error->error.httpStatus, 401);
        QCOMPARE(error->error.message, QStringLiteral("Incorrect API key provided."));
    }

    void http404SurfacesOllamaErrorMessage()
    {
        MockHttpServer::Script script;
        script.status = 404;
        script.statusText = "Not Found";
        script.contentType = "application/json";
        script.chunks = {fixtures::ollamaErrorBody()};
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.httpStatus, 404);
        QCOMPARE(error->error.message, QStringLiteral("model not found, try pulling it first"));
    }

    void http500NonJsonBodyKeepsSnippet()
    {
        MockHttpServer::Script script;
        script.status = 500;
        script.statusText = "Internal Server Error";
        script.contentType = "text/plain";
        script.chunks = {QByteArrayLiteral("upstream exploded")};
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Http);
        QCOMPARE(error->error.httpStatus, 500);
        QVERIFY(error->error.message.contains(QStringLiteral("upstream exploded")));
    }

    void garbageChunkIsParseError()
    {
        MockHttpServer::Script script;
        script.chunks = {QByteArrayLiteral("data: {nope\n\n")};
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Parse);
    }

    void connectionDropMidStreamIsNetworkError()
    {
        MockHttpServer::Script script =
            sseScript(fixtures::wire("data: {'choices':[{'index':0,'delta':{'content':'Hi'}}]}\n\n"));
        script.dropAfterChunks = true;
        const auto events = runScript(script);
        QCOMPARE(events.size(), 2); // the delta got through, then the drop
        QCOMPARE(std::get<TextDelta>(events.first()).text, QStringLiteral("Hi"));
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Network);
    }

    void cleanEndWithoutCompletionIsParseError()
    {
        const auto events = runScript(sseScript(fixtures::truncatedStream()));
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Parse);
    }

    void finishReasonWithoutSentinelStillSucceeds()
    {
        const auto events = runScript(sseScript(fixtures::noSentinelStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::EndTurn);
        QVERIFY(!done->usage.has_value());
    }

    void malformedToolArgsIsParseErrorAtFinish()
    {
        const auto events = runScript(sseScript(fixtures::malformedArgsStream()));
        QCOMPARE(events.size(), 3); // Start, ArgsDelta, then the terminal
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Parse);
    }

    void cancelMidStream()
    {
        MockHttpServer::Script script =
            sseScript(fixtures::wire("data: {'choices':[{'index':0,'delta':{'content':'Hi'}}]}\n\n"));
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        OpenAiCompatProvider provider(config());
        auto future = provider.generate(simpleRequest());
        QVERIFY(waitForResults(future, 1));
        provider.cancel();
        QVERIFY(waitFinished(future));
        const auto events = future.results();
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Cancelled);
    }

    void stallTimeoutMapsToTimeout()
    {
        MockHttpServer::Script script =
            sseScript(fixtures::wire("data: {'choices':[{'index':0,'delta':{'content':'Hi'}}]}\n\n"));
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        OpenAiCompatConfig cfg = config();
        cfg.stallTimeout = std::chrono::milliseconds(300);
        OpenAiCompatProvider provider(cfg);
        auto future = provider.generate(simpleRequest());
        QVERIFY(waitFinished(future));
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Timeout);
    }

    void secondGenerateWhileInFlightFailsFast()
    {
        MockHttpServer::Script script;
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        OpenAiCompatProvider provider(config());
        auto first = provider.generate(simpleRequest());
        auto second = provider.generate(simpleRequest());
        QVERIFY(second.isFinished()); // rejected without touching the network
        const ErrorEvent *rejection = terminalError(second.results());
        QVERIFY(rejection);
        QCOMPARE(rejection->error.code, AgentError::Code::Provider);
        provider.cancel();
        QVERIFY(waitFinished(first));
        const ErrorEvent *cancelled = terminalError(first.results());
        QVERIFY(cancelled);
        QCOMPARE(cancelled->error.code, AgentError::Code::Cancelled);
    }

    void destructionMidFlightFinishesFuture()
    {
        MockHttpServer::Script script =
            sseScript(fixtures::wire("data: {'choices':[{'index':0,'delta':{'content':'Hi'}}]}\n\n"));
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        auto provider = std::make_unique<OpenAiCompatProvider>(config());
        auto future = provider->generate(simpleRequest());
        QVERIFY(waitForResults(future, 1));
        provider.reset(); // must not leave the future hanging (iprovider.h contract)
        QVERIFY(future.isFinished());
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Cancelled);
    }

    void imageRequestEncodesAndStreams()
    {
        // A user image now encodes (data URL) and the request reaches the network and completes.
        InferenceRequest request = simpleRequest();
        request.messages.first().blocks.append(
            ImageBlock{QByteArrayLiteral("png"), QStringLiteral("image/png")});
        const auto events = runScript(sseScript(fixtures::openaiTextStream()), request);
        QVERIFY(terminalDone(events)); // streamed to completion, no encode error
        const QByteArray body = m_server.lastRequestBody();
        QVERIFY(body.contains("image_url"));
        QVERIFY(body.contains(QByteArrayLiteral("png").toBase64())); // the bytes rode the wire
    }

    void notReadyWithoutBaseUrl()
    {
        OpenAiCompatProvider provider(OpenAiCompatConfig{});
        QVERIFY(!provider.isReady());
        QVERIFY(OpenAiCompatProvider(config()).isReady());
    }
};

QTEST_GUILESS_MAIN(TestOpenAiCompatProvider)
#include "test_openaicompatprovider.moc"
