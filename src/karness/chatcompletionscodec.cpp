// SPDX-License-Identifier: GPL-3.0-or-later
#include "chatcompletionscodec.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <variant>

namespace karness::chatcompletions {

namespace {

QString roleString(Role role)
{
    switch (role) {
    case Role::System: return QStringLiteral("system");
    case Role::User: return QStringLiteral("user");
    case Role::Assistant: return QStringLiteral("assistant");
    case Role::Tool: return QStringLiteral("tool");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString joinedText(const QList<ContentBlock> &blocks)
{
    QStringList parts;
    for (const ContentBlock &block : blocks)
        if (const auto *text = std::get_if<TextBlock>(&block); text && !text->text.isEmpty())
            parts.append(text->text);
    return parts.join(QStringLiteral("\n\n"));
}

// A base64 data URL — the Chat Completions image wire form: image_url.url = "data:<mime>;base64,…".
QString imageDataUrl(const ImageBlock &image)
{
    const QString mime = image.mimeType.isEmpty() ? QStringLiteral("image/jpeg") : image.mimeType;
    return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(image.data.toBase64()));
}

// A user/system message's content: a plain string when text-only, or a content-array
// ([{type:text,…},{type:image_url,…}]) when it carries any images. Images are input-only —
// in the "photos in journal" scope they reach a user turn only via the tool-result hoist below.
QJsonValue userContent(const QList<ContentBlock> &blocks)
{
    bool hasImage = false;
    for (const ContentBlock &block : blocks)
        if (std::holds_alternative<ImageBlock>(block))
            hasImage = true;
    if (!hasImage)
        return joinedText(blocks);

    QJsonArray content;
    const QString text = joinedText(blocks);
    if (!text.isEmpty())
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                   {QStringLiteral("text"), text}});
    for (const ContentBlock &block : blocks)
        if (const auto *image = std::get_if<ImageBlock>(&block))
            content.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image_url")},
                {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), imageDataUrl(*image)}}}});
    return content;
}

// One {"role":"tool"} wire message per ToolResultBlock — Chat Completions has no multi-result
// message, and a tool message is TEXT-ONLY. Any images a tool returned cannot ride the tool message,
// so they are HOISTED into one following {"role":"user"} message (ADR 0019 / 0025: the
// tool-result→user-block workaround). isError has no wire form; the tool layer phrases error text.
void appendToolResults(QJsonArray &out, const Message &message)
{
    QJsonArray hoistedImages;
    QStringList imageCallIds;
    for (const ContentBlock &block : message.blocks) {
        const auto *result = std::get_if<ToolResultBlock>(&block);
        if (!result)
            continue;
        QStringList parts;
        bool hasImage = false;
        for (const ContentPart &part : result->parts) {
            if (const auto *text = std::get_if<TextBlock>(&part); text && !text->text.isEmpty())
                parts.append(text->text);
            else if (const auto *image = std::get_if<ImageBlock>(&part)) {
                hasImage = true;
                hoistedImages.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("image_url")},
                    {QStringLiteral("image_url"),
                     QJsonObject{{QStringLiteral("url"), imageDataUrl(*image)}}}});
            }
        }
        // A tool message must be non-empty; if the result was image-only, anchor it with a note that
        // points at the hoisted user message below.
        if (parts.isEmpty() && hasImage)
            parts.append(QStringLiteral("[see image below]"));
        if (hasImage)
            imageCallIds.append(result->callId);
        out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                               {QStringLiteral("tool_call_id"), result->callId},
                               {QStringLiteral("content"), parts.join(QLatin1Char('\n'))}});
    }
    // One synthetic user message carrying every image returned by this turn's tool calls, placed
    // immediately after the tool messages (the natural in-order position) and before the next turn.
    if (!hoistedImages.isEmpty()) {
        QJsonArray content;
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"),
             QStringLiteral("Images returned by tool call(s): %1")
                 .arg(imageCallIds.join(QStringLiteral(", ")))}});
        for (const QJsonValue &img : hoistedImages)
            content.append(img);
        out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                               {QStringLiteral("content"), content}});
    }
}

void appendAssistant(QJsonArray &out, const Message &message)
{
    QJsonObject wire{{QStringLiteral("role"), QStringLiteral("assistant")}};
    QJsonArray calls;
    for (const ContentBlock &block : message.blocks)
        if (const auto *call = std::get_if<ToolCallBlock>(&block))
            calls.append(QJsonObject{
                {QStringLiteral("id"), call->id},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"),
                 QJsonObject{{QStringLiteral("name"), call->name},
                             {QStringLiteral("arguments"),
                              QString::fromUtf8(
                                  QJsonDocument(call->args).toJson(QJsonDocument::Compact))}}}});
    const QString text = joinedText(message.blocks);
    if (calls.isEmpty()) {
        wire.insert(QStringLiteral("content"), text);
    } else {
        wire.insert(QStringLiteral("content"),
                    text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text));
        wire.insert(QStringLiteral("tool_calls"), calls);
    }
    out.append(wire);
}

} // namespace

std::expected<QJsonObject, AgentError> encodeRequest(const InferenceRequest &request)
{
    // request.cacheStablePrefix is intentionally ignored: OpenAI-compatible prompt
    // caching is automatic on a stable leading prefix (no wire marker exists), so the
    // benefit comes from the request shape alone (docs/adr/0019 cache-placement).
    QJsonArray messages;
    for (const Message &message : request.messages) {
        switch (message.role) {
        case Role::System:
        case Role::User:
            // userContent() emits a content-array when the turn carries images, else a string.
            messages.append(QJsonObject{{QStringLiteral("role"), roleString(message.role)},
                                        {QStringLiteral("content"), userContent(message.blocks)}});
            break;
        case Role::Assistant:
            appendAssistant(messages, message);
            break;
        case Role::Tool:
            appendToolResults(messages, message);
            break;
        }
    }

    QJsonObject body{
        {QStringLiteral("model"), request.model},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("stream"), true},
        {QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}}}};

    if (!request.tools.isEmpty()) { // omitted when empty: some servers 400 on []
        QJsonArray tools;
        for (const ToolSpec &tool : request.tools)
            tools.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"),
                 QJsonObject{{QStringLiteral("name"), tool.name},
                             {QStringLiteral("description"), tool.description},
                             {QStringLiteral("parameters"), tool.inputSchema}}}});
        body.insert(QStringLiteral("tools"), tools);
    }
    if (request.temperature)
        body.insert(QStringLiteral("temperature"), *request.temperature);
    if (request.seed)
        body.insert(QStringLiteral("seed"), static_cast<qint64>(*request.seed));
    if (request.maxTokens)
        body.insert(QStringLiteral("max_tokens"), *request.maxTokens);
    switch (request.reasoningEffort) { // omitted when Off
    case ReasoningEffort::Off:
        break;
    case ReasoningEffort::Low:
        body.insert(QStringLiteral("reasoning_effort"), QStringLiteral("low"));
        break;
    case ReasoningEffort::Medium:
        body.insert(QStringLiteral("reasoning_effort"), QStringLiteral("medium"));
        break;
    case ReasoningEffort::High:
        body.insert(QStringLiteral("reasoning_effort"), QStringLiteral("high"));
        break;
    }
    return body;
}

std::expected<DecodedChunk, AgentError> decodeChunk(const QByteArray &data)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return std::unexpected(
            AgentError{AgentError::Code::Parse,
                       QStringLiteral("malformed stream chunk: %1").arg(parseError.errorString()),
                       {}});
    const QJsonObject root = doc.object();
    DecodedChunk chunk;

    if (const QJsonValue usage = root.value(QStringLiteral("usage")); usage.isObject()) {
        TokenUsage tokens;
        const QJsonObject object = usage.toObject();
        if (const QJsonValue v = object.value(QStringLiteral("prompt_tokens")); v.isDouble())
            tokens.inputTokens = v.toInteger();
        if (const QJsonValue v = object.value(QStringLiteral("completion_tokens")); v.isDouble())
            tokens.outputTokens = v.toInteger();
        chunk.usage = tokens;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) // OpenAI's usage-only final chunk has "choices": []
        return chunk;
    const QJsonObject choice = choices.first().toObject();
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();

    // Reasoning before content: models reason first, and a compat server may
    // put both fields in one chunk.
    QString reasoning = delta.value(QStringLiteral("reasoning_content")).toString(); // DeepSeek
    if (reasoning.isEmpty())
        reasoning = delta.value(QStringLiteral("reasoning")).toString(); // OpenRouter
    if (!reasoning.isEmpty())
        chunk.events.append(ReasoningDelta{reasoning});

    if (const QString content = delta.value(QStringLiteral("content")).toString(); !content.isEmpty())
        chunk.events.append(TextDelta{content});

    const QJsonArray calls = delta.value(QStringLiteral("tool_calls")).toArray();
    for (qsizetype position = 0; position < calls.size(); ++position) {
        const QJsonObject entry = calls.at(position).toObject();
        const int index = entry.contains(QStringLiteral("index"))
                              ? entry.value(QStringLiteral("index")).toInt()
                              : static_cast<int>(position);
        const QJsonObject function = entry.value(QStringLiteral("function")).toObject();
        const QString name = function.value(QStringLiteral("name")).toString();
        const QString args = function.value(QStringLiteral("arguments")).toString();
        if (!name.isEmpty())
            chunk.events.append(ToolCallStart{index, entry.value(QStringLiteral("id")).toString(), name});
        if (!args.isEmpty())
            chunk.events.append(ToolCallArgsDelta{index, args});
    }

    if (const QJsonValue finish = choice.value(QStringLiteral("finish_reason")); finish.isString())
        chunk.stopReason = mapFinishReason(finish.toString());
    return chunk;
}

StopReason mapFinishReason(const QString &finishReason)
{
    if (finishReason == QStringLiteral("stop"))
        return StopReason::EndTurn;
    if (finishReason == QStringLiteral("tool_calls") || finishReason == QStringLiteral("function_call"))
        return StopReason::ToolCalls;
    if (finishReason == QStringLiteral("length"))
        return StopReason::MaxTokens;
    if (finishReason == QStringLiteral("content_filter"))
        return StopReason::ContentFilter;
    return StopReason::Other;
}

} // namespace karness::chatcompletions
