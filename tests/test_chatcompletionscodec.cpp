// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "chatcompletionscodec.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

using namespace karness;
using namespace karness::chatcompletions;

namespace {

// moc mis-lexes raw string literals (it pairs quotes and counts braces as
// if they were code, then silently drops every class that follows), so JSON
// test bytes write ` for \" — this helper converts. Two rules keep moc in
// sync: never write \" inside a raw string, and keep braces balanced within
// each quoted JSON value (no "{x" fragments — use a normal escaped literal
// for intentionally truncated JSON).
QByteArray wire(const char *singleQuoted)
{
    QByteArray text(singleQuoted);
    text.replace('`', "\\\"");
    text.replace('\'', '"');
    return text;
}

QJsonObject json(const char *literal)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(wire(literal), &error);
    Q_ASSERT(error.error == QJsonParseError::NoError);
    return doc.object();
}

Message userText(const QString &text)
{
    return Message{Role::User, {TextBlock{text}}};
}

} // namespace

class TestChatCompletionsCodec : public QObject {
    Q_OBJECT
private slots:
    // ---- encodeRequest ----

    void minimalRequestGolden()
    {
        InferenceRequest request;
        request.model = QStringLiteral("qwen3");
        request.messages = {userText(QStringLiteral("hi"))};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        QCOMPARE(*body, json(R"({
            "model": "qwen3",
            "messages": [{"role": "user", "content": "hi"}],
            "stream": true,
            "stream_options": {"include_usage": true}
        })"));
        // Unset optionals must be ABSENT, not null/zero (some providers
        // reject temperature on reasoning models).
        QVERIFY(!body->contains(QStringLiteral("temperature")));
        QVERIFY(!body->contains(QStringLiteral("seed")));
        QVERIFY(!body->contains(QStringLiteral("max_tokens")));
        QVERIFY(!body->contains(QStringLiteral("reasoning_effort")));
        QVERIFY(!body->contains(QStringLiteral("tools")));
    }

    void allOptionsSet()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {userText(QStringLiteral("x"))};
        request.temperature = 0.2;
        request.seed = 42;
        request.maxTokens = 512;
        request.reasoningEffort = ReasoningEffort::High;
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        QCOMPARE(body->value(QStringLiteral("temperature")).toDouble(), 0.2);
        QCOMPARE(body->value(QStringLiteral("seed")).toInt(), 42);
        QCOMPARE(body->value(QStringLiteral("max_tokens")).toInt(), 512);
        QCOMPARE(body->value(QStringLiteral("reasoning_effort")).toString(), QStringLiteral("high"));
    }

    void reasoningEffortValues()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.reasoningEffort = ReasoningEffort::Low;
        QCOMPARE(encodeRequest(request)->value(QStringLiteral("reasoning_effort")).toString(),
                 QStringLiteral("low"));
        request.reasoningEffort = ReasoningEffort::Medium;
        QCOMPARE(encodeRequest(request)->value(QStringLiteral("reasoning_effort")).toString(),
                 QStringLiteral("medium"));
    }

    void toolsShape()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {userText(QStringLiteral("x"))};
        request.tools = {ToolSpec{QStringLiteral("read_plant_data"),
                                  QStringLiteral("Latest readings for one plant"),
                                  json(R"({"type": "object",
                                           "properties": {"plantId": {"type": "string"}},
                                           "required": ["plantId"]})")}};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        QCOMPARE(body->value(QStringLiteral("tools")).toArray(), QJsonArray{json(R"({
            "type": "function",
            "function": {
                "name": "read_plant_data",
                "description": "Latest readings for one plant",
                "parameters": {"type": "object",
                               "properties": {"plantId": {"type": "string"}},
                               "required": ["plantId"]}
            }
        })")});
    }

    void systemMessage()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {Message{Role::System, {TextBlock{QStringLiteral("be brief")}}},
                            userText(QStringLiteral("x"))};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        const QJsonObject first = body->value(QStringLiteral("messages")).toArray().first().toObject();
        QCOMPARE(first.value(QStringLiteral("role")).toString(), QStringLiteral("system"));
        QCOMPARE(first.value(QStringLiteral("content")).toString(), QStringLiteral("be brief"));
    }

    void multipleTextBlocksJoined()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {
            Message{Role::User, {TextBlock{QStringLiteral("a")}, TextBlock{QStringLiteral("b")}}}};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        const QJsonObject wire = body->value(QStringLiteral("messages")).toArray().first().toObject();
        QCOMPARE(wire.value(QStringLiteral("content")).toString(), QStringLiteral("a\n\nb"));
    }

    void assistantTextAndToolCalls()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {Message{
            Role::Assistant,
            {TextBlock{QStringLiteral("Checking…")},
             ToolCallBlock{QStringLiteral("c1"), QStringLiteral("list_plants"), QJsonObject()},
             ToolCallBlock{QStringLiteral("c2"), QStringLiteral("read_plant_data"),
                           json(R"({"plantId": "p-1"})")}}}};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        const QJsonObject wire = body->value(QStringLiteral("messages")).toArray().first().toObject();
        QCOMPARE(wire, json(R"({
            "role": "assistant",
            "content": "Checking…",
            "tool_calls": [
                {"id": "c1", "type": "function",
                 "function": {"name": "list_plants", "arguments": "{}"}},
                {"id": "c2", "type": "function",
                 "function": {"name": "read_plant_data", "arguments": "{`plantId`:`p-1`}"}}
            ]
        })"));
    }

    void assistantToolCallsWithoutTextHasNullContent()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {Message{
            Role::Assistant,
            {ToolCallBlock{QStringLiteral("c1"), QStringLiteral("list_plants"), QJsonObject()}}}};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        const QJsonObject wire = body->value(QStringLiteral("messages")).toArray().first().toObject();
        QVERIFY(wire.value(QStringLiteral("content")).isNull());
    }

    void toolResultsBecomeSeparateToolMessages()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {Message{
            Role::Tool,
            {ToolResultBlock{QStringLiteral("c1"), {TextBlock{QStringLiteral("3 plants")}}, false},
             ToolResultBlock{QStringLiteral("c2"),
                             {TextBlock{QStringLiteral("soil 12%")}, TextBlock{QStringLiteral("dry")}},
                             true}}}};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        const QJsonArray messages = body->value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject(), json(R"({
            "role": "tool", "tool_call_id": "c1", "content": "3 plants"
        })"));
        // isError has no wire form; parts join with '\n'.
        QCOMPARE(messages.at(1).toObject(), json(R"({
            "role": "tool", "tool_call_id": "c2", "content": "soil 12%\ndry"
        })"));
    }

    void userImageEncodesAsImageUrl()
    {
        // A user turn with an image becomes a content-ARRAY of text + image_url(data URL).
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {Message{
            Role::User, {TextBlock{QStringLiteral("what's wrong?")},
                         ImageBlock{QByteArrayLiteral("png..."), QStringLiteral("image/png")}}}};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        const QJsonArray messages = body->value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 1);
        const QJsonObject msg = messages.first().toObject();
        QCOMPARE(msg.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const QJsonArray content = msg.value(QStringLiteral("content")).toArray();
        QCOMPARE(content.size(), 2);
        QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("text"));
        const QJsonObject img = content.at(1).toObject();
        QCOMPARE(img.value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));
        const QString url =
            img.value(QStringLiteral("image_url")).toObject().value(QStringLiteral("url")).toString();
        QVERIFY(url.startsWith(QStringLiteral("data:image/png;base64,")));
        QCOMPARE(url.section(QLatin1Char(','), 1),
                 QString::fromLatin1(QByteArrayLiteral("png...").toBase64()));
    }

    void toolResultImagesHoistToOneUserMessage()
    {
        // Tool messages are text-only, so images returned by tools are hoisted into ONE following
        // user message (image-only results get a "[see image below]" anchor).
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {Message{
            Role::Tool,
            {ToolResultBlock{QStringLiteral("c1"),
                             {TextBlock{QStringLiteral("photo of leaf")},
                              ImageBlock{QByteArrayLiteral("img1"), QStringLiteral("image/jpeg")}},
                             false},
             ToolResultBlock{QStringLiteral("c2"),
                             {ImageBlock{QByteArrayLiteral("img2"), QStringLiteral("image/png")}},
                             false}}}};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        const QJsonArray messages = body->value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 3); // two tool messages + one hoisted user message
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("content")).toString(),
                 QStringLiteral("photo of leaf"));
        QCOMPARE(messages.at(1).toObject().value(QStringLiteral("content")).toString(),
                 QStringLiteral("[see image below]")); // image-only result anchored
        const QJsonObject hoisted = messages.at(2).toObject();
        QCOMPARE(hoisted.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const QJsonArray content = hoisted.value(QStringLiteral("content")).toArray();
        QCOMPARE(content.size(), 3); // text note + two images
        QCOMPARE(content.at(1).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("image_url"));
        QCOMPARE(content.at(2).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("image_url"));
    }

    void reasoningBlockIsDropped()
    {
        InferenceRequest request;
        request.model = QStringLiteral("m");
        request.messages = {Message{Role::Assistant,
                                    {ReasoningBlock{QStringLiteral("soil is dry…"), QJsonObject()},
                                     TextBlock{QStringLiteral("Water it.")}}}};
        const auto body = encodeRequest(request);
        QVERIFY(body.has_value());
        const QJsonObject wire = body->value(QStringLiteral("messages")).toArray().first().toObject();
        QCOMPARE(wire.value(QStringLiteral("content")).toString(), QStringLiteral("Water it."));
        QVERIFY(!QJsonDocument(wire).toJson().contains("soil is dry"));
    }

    // ---- decodeChunk ----

    void textDelta()
    {
        const auto chunk = decodeChunk(R"({"choices":[{"index":0,"delta":{"content":"Hi"}}]})");
        QVERIFY(chunk.has_value());
        QCOMPARE(chunk->events, {StreamEvent(TextDelta{QStringLiteral("Hi")})});
        QVERIFY(!chunk->stopReason);
        QVERIFY(!chunk->usage);
    }

    void roleOnlyFirstChunkYieldsNoEvents()
    {
        // OpenAI's first chunk: role + empty content.
        const auto chunk =
            decodeChunk(R"({"choices":[{"index":0,"delta":{"role":"assistant","content":""}}]})");
        QVERIFY(chunk.has_value());
        QVERIFY(chunk->events.isEmpty());
    }

    void nullContentYieldsNoEvents()
    {
        const auto chunk = decodeChunk(R"({"choices":[{"index":0,"delta":{"content":null}}]})");
        QVERIFY(chunk.has_value());
        QVERIFY(chunk->events.isEmpty());
    }

    void toolCallStartChunk()
    {
        const auto chunk = decodeChunk(
            R"({"choices":[{"index":0,"delta":{"tool_calls":[
                {"index":0,"id":"call_1","type":"function",
                 "function":{"name":"list_plants","arguments":""}}]}}]})");
        QVERIFY(chunk.has_value());
        QCOMPARE(chunk->events,
                 {StreamEvent(ToolCallStart{0, QStringLiteral("call_1"), QStringLiteral("list_plants")})});
    }

    void toolCallArgsDeltaChunk()
    {
        // A name-less tool_calls entry is an args continuation; the fragment
        // arrives as raw JSON text (brace-free here per the wire() rules).
        const auto chunk = decodeChunk(wire(
            R"({"choices":[{"index":0,"delta":{"tool_calls":[
                {"index":0,"function":{"arguments":"plantId`:`p-"}}]}}]})"));
        QVERIFY(chunk.has_value());
        QCOMPARE(chunk->events, {StreamEvent(ToolCallArgsDelta{0, QStringLiteral("plantId\":\"p-")})});
    }

    void ollamaSingleChunkCompleteCall()
    {
        // Ollama sends the whole call in one chunk: Start AND ArgsDelta.
        const auto chunk = decodeChunk(wire(
            R"({"choices":[{"index":0,"delta":{"tool_calls":[
                {"index":0,"id":"call_x","type":"function",
                 "function":{"name":"read_plant_data","arguments":"{`plantId`:`p-1`}"}}]}}]})"));
        QVERIFY(chunk.has_value());
        QCOMPARE(chunk->events.size(), 2);
        QCOMPARE(std::get<ToolCallStart>(chunk->events.at(0)).name, QStringLiteral("read_plant_data"));
        QCOMPARE(std::get<ToolCallArgsDelta>(chunk->events.at(1)).argsDelta,
                 QStringLiteral("{\"plantId\":\"p-1\"}"));
    }

    void missingIndexFallsBackToPosition()
    {
        const auto chunk = decodeChunk(
            R"({"choices":[{"index":0,"delta":{"tool_calls":[
                {"id":"c1","function":{"name":"a","arguments":""}},
                {"id":"c2","function":{"name":"b","arguments":""}}]}}]})");
        QVERIFY(chunk.has_value());
        QCOMPARE(std::get<ToolCallStart>(chunk->events.at(0)).index, 0);
        QCOMPARE(std::get<ToolCallStart>(chunk->events.at(1)).index, 1);
    }

    void parallelCallsInOneChunk()
    {
        const auto chunk = decodeChunk(
            R"({"choices":[{"index":0,"delta":{"tool_calls":[
                {"index":0,"id":"c1","function":{"name":"a","arguments":"{}"}},
                {"index":1,"id":"c2","function":{"name":"b","arguments":"{}"}}]}}]})");
        QVERIFY(chunk.has_value());
        QCOMPARE(chunk->events.size(), 4); // Start+Args per call
        QCOMPARE(std::get<ToolCallStart>(chunk->events.at(2)).index, 1);
    }

    void reasoningContentField()
    {
        const auto chunk =
            decodeChunk(R"({"choices":[{"index":0,"delta":{"reasoning_content":"hmm"}}]})");
        QVERIFY(chunk.has_value());
        QCOMPARE(chunk->events, {StreamEvent(ReasoningDelta{QStringLiteral("hmm")})});
    }

    void reasoningField()
    {
        const auto chunk = decodeChunk(R"({"choices":[{"index":0,"delta":{"reasoning":"hmm"}}]})");
        QVERIFY(chunk.has_value());
        QCOMPARE(chunk->events, {StreamEvent(ReasoningDelta{QStringLiteral("hmm")})});
    }

    void finishReasonLatched()
    {
        const auto chunk =
            decodeChunk(R"({"choices":[{"index":0,"delta":{},"finish_reason":"tool_calls"}]})");
        QVERIFY(chunk.has_value());
        QVERIFY(chunk->events.isEmpty());
        QCOMPARE(chunk->stopReason, StopReason::ToolCalls);
    }

    void usageOnlyFinalChunk()
    {
        // OpenAI with stream_options.include_usage: last chunk, empty choices.
        const auto chunk = decodeChunk(
            R"({"choices":[],"usage":{"prompt_tokens":12,"completion_tokens":34,"total_tokens":46}})");
        QVERIFY(chunk.has_value());
        QVERIFY(chunk->events.isEmpty());
        QVERIFY(chunk->usage.has_value());
        QCOMPARE(chunk->usage->inputTokens, 12);
        QCOMPARE(chunk->usage->outputTokens, 34);
    }

    void unknownFieldsIgnored()
    {
        // llama.cpp server appends "timings"; OpenAI adds system_fingerprint.
        const auto chunk = decodeChunk(
            R"({"choices":[{"index":0,"delta":{"content":"x"}}],
                "system_fingerprint":"fp_1","timings":{"predicted_per_second":42.0}})");
        QVERIFY(chunk.has_value());
        QCOMPARE(chunk->events.size(), 1);
    }

    void garbageJsonIsParseError()
    {
        const auto chunk = decodeChunk("{\"choices\": [");
        QVERIFY(!chunk.has_value());
        QCOMPARE(chunk.error().code, AgentError::Code::Parse);
    }

    void nonObjectIsParseError()
    {
        QVERIFY(!decodeChunk("[1,2]").has_value());
    }

    void finishReasonMapping()
    {
        QCOMPARE(mapFinishReason(QStringLiteral("stop")), StopReason::EndTurn);
        QCOMPARE(mapFinishReason(QStringLiteral("tool_calls")), StopReason::ToolCalls);
        QCOMPARE(mapFinishReason(QStringLiteral("function_call")), StopReason::ToolCalls);
        QCOMPARE(mapFinishReason(QStringLiteral("length")), StopReason::MaxTokens);
        QCOMPARE(mapFinishReason(QStringLiteral("content_filter")), StopReason::ContentFilter);
        QCOMPARE(mapFinishReason(QStringLiteral("eos")), StopReason::Other);
    }
};

QTEST_GUILESS_MAIN(TestChatCompletionsCodec)
#include "test_chatcompletionscodec.moc"
