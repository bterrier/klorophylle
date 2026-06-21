// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "karnessfixtures.h"
#include "mockhttpserver.h"
#include "responsesprovider.h"

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
    request.model = QStringLiteral("gpt-test");
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

class TestResponsesProvider : public QObject {
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
        ResponsesProvider provider(config());
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
        const auto events = runScript(sseScript(fixtures::responsesTextStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::EndTurn);
        QVERIFY(done->usage.has_value());
        QCOMPARE(done->usage->inputTokens, 11);
        QCOMPARE(done->usage->outputTokens, 22);
        QCOMPARE(done->message.blocks.size(), 1);
        QCOMPARE(std::get<TextBlock>(done->message.blocks.first()).text,
                 QStringLiteral("Your basil needs water."));
    }

    void toolCallAssembled()
    {
        const auto events = runScript(sseScript(fixtures::responsesToolCallStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::ToolCalls);
        const auto &call = std::get<ToolCallBlock>(done->message.blocks.first());
        QCOMPARE(call.id, QStringLiteral("call_x"));
        QCOMPARE(call.name, QStringLiteral("read_plant_data"));
        QCOMPARE(call.args.value(QStringLiteral("plantId")).toString(), QStringLiteral("p-1"));
    }

    void reasoningCarriesEncryptedContent()
    {
        const auto events = runScript(sseScript(fixtures::responsesReasoningStream()));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->message.blocks.size(), 2);
        const auto &reasoning = std::get<ReasoningBlock>(done->message.blocks.at(0));
        QCOMPARE(reasoning.text, QStringLiteral("checking moisture"));
        QCOMPARE(reasoning.providerOpaque.value(QStringLiteral("encrypted_content")).toString(),
                 QStringLiteral("enc-blob"));
        QCOMPARE(reasoning.providerOpaque.value(QStringLiteral("id")).toString(),
                 QStringLiteral("rs_1"));
        QCOMPARE(std::get<TextBlock>(done->message.blocks.at(1)).text, QStringLiteral("Water it."));
    }

    void requestWireShape()
    {
        InferenceRequest request = simpleRequest();
        request.messages.prepend(Message{Role::System, {TextBlock{QStringLiteral("You are a gardener.")}}});
        request.tools = {ToolSpec{QStringLiteral("list_plants"), QStringLiteral("d"), QJsonObject()}};
        request.reasoningEffort = ReasoningEffort::High;
        m_server.setScript(sseScript(fixtures::responsesTextStream()));
        ProviderConfig cfg = config();
        cfg.apiKey = QStringLiteral("sk-test");
        ResponsesProvider provider(cfg);
        auto future = provider.generate(request);
        QVERIFY(waitFinished(future));

        const QByteArray headers = m_server.lastRequestHeaders().toLower();
        QVERIFY(headers.startsWith("post /v1/responses http/1.1"));
        QVERIFY(headers.contains("authorization: bearer sk-test"));

        const QJsonObject body = QJsonDocument::fromJson(m_server.lastRequestBody()).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-test"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);
        // System hoisted to instructions; input has the single user item.
        QCOMPARE(body.value(QStringLiteral("instructions")).toString(),
                 QStringLiteral("You are a gardener."));
        QCOMPARE(body.value(QStringLiteral("input")).toArray().size(), 1);
        // Tools are FLAT (name at the top of the object, not nested under "function").
        const QJsonObject tool = body.value(QStringLiteral("tools")).toArray().first().toObject();
        QCOMPARE(tool.value(QStringLiteral("type")).toString(), QStringLiteral("function"));
        QCOMPARE(tool.value(QStringLiteral("name")).toString(), QStringLiteral("list_plants"));
        QVERIFY(tool.contains(QStringLiteral("parameters")));
        // Reasoning effort + stateless encrypted-reasoning opt-in.
        QCOMPARE(body.value(QStringLiteral("reasoning")).toObject().value(QStringLiteral("effort")).toString(),
                 QStringLiteral("high"));
        QCOMPARE(body.value(QStringLiteral("store")).toBool(true), false);
        QVERIFY(body.value(QStringLiteral("include"))
                    .toArray()
                    .contains(QStringLiteral("reasoning.encrypted_content")));
    }

    void failedEventSurfacesAsProviderError()
    {
        const auto events = runScript(sseScript(fixtures::responsesFailedStream()));
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Provider);
        QCOMPARE(error->error.message, QStringLiteral("boom"));
    }

    void http401SurfacesErrorMessage()
    {
        MockHttpServer::Script script;
        script.status = 401;
        script.statusText = "Unauthorized";
        script.contentType = "application/json";
        script.chunks = {fixtures::openaiErrorBody401()};
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Http);
        QCOMPARE(error->error.httpStatus, 401);
        QCOMPARE(error->error.message, QStringLiteral("Incorrect API key provided."));
    }

    void connectionDropMidStreamIsNetworkError()
    {
        MockHttpServer::Script script = sseScript(fixtures::responsesPartialStream());
        script.dropAfterChunks = true;
        const auto events = runScript(script);
        const ErrorEvent *error = terminalError(events);
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Network);
    }

    void cancelMidStream()
    {
        MockHttpServer::Script script = sseScript(fixtures::responsesPartialStream());
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        ResponsesProvider provider(config());
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
        MockHttpServer::Script script = sseScript(fixtures::responsesPartialStream());
        script.holdOpenAtEnd = true;
        m_server.setScript(script);
        ProviderConfig cfg = config();
        cfg.stallTimeout = std::chrono::milliseconds(300);
        ResponsesProvider provider(cfg);
        auto future = provider.generate(simpleRequest());
        QVERIFY(waitFinished(future));
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Timeout);
    }

    void imageRequestEncodesAndStreams()
    {
        // A user image encodes as an input_image (data URL) and the request streams to done.
        InferenceRequest request = simpleRequest();
        request.messages.first().blocks.append(
            ImageBlock{QByteArrayLiteral("png"), QStringLiteral("image/png")});
        const auto events = runScript(sseScript(fixtures::responsesTextStream()), request);
        QVERIFY(terminalDone(events));
        const QByteArray body = m_server.lastRequestBody();
        QVERIFY(body.contains(QByteArrayLiteral("input_image")));
        QVERIFY(body.contains(QByteArrayLiteral("png").toBase64()));
    }
};

QTEST_GUILESS_MAIN(TestResponsesProvider)
#include "test_responsesprovider.moc"
