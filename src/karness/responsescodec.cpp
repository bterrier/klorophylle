// SPDX-License-Identifier: GPL-3.0-or-later
#include "responsescodec.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <variant>

namespace karness::responses {

namespace {

QString joinedText(const QList<ContentBlock> &blocks)
{
    QStringList parts;
    for (const ContentBlock &block : blocks)
        if (const auto *text = std::get_if<TextBlock>(&block); text && !text->text.isEmpty())
            parts.append(text->text);
    return parts.join(QStringLiteral("\n\n"));
}

// A base64 data URL — Responses' input_image takes image_url as a string.
QString imageDataUrl(const ImageBlock &image)
{
    const QString mime = image.mimeType.isEmpty() ? QStringLiteral("image/jpeg") : image.mimeType;
    return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(image.data.toBase64()));
}

// A user input item: plain {role,content:string} when text-only, else a content-array of
// input_text + input_image parts. `images` may add to the message's own image blocks (used by
// the tool-result hoist below to carry images on a synthesized user turn).
QJsonObject userInput(const QList<ContentBlock> &blocks, const QList<ImageBlock> &extraImages = {})
{
    QList<ImageBlock> images = extraImages;
    for (const ContentBlock &block : blocks)
        if (const auto *image = std::get_if<ImageBlock>(&block))
            images.append(*image);
    const QString text = joinedText(blocks);
    if (images.isEmpty())
        return QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                           {QStringLiteral("content"), text}};

    QJsonArray content;
    if (!text.isEmpty())
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                                   {QStringLiteral("text"), text}});
    for (const ImageBlock &image : images)
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")},
                                   {QStringLiteral("image_url"), imageDataUrl(image)}});
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                       {QStringLiteral("content"), content}};
}

void appendAssistant(QJsonArray &input, const Message &message)
{
    // Reasoning items first (must precede what they produced), then the message
    // text, then function_call items — the order Responses expects on replay.
    for (const ContentBlock &block : message.blocks) {
        const auto *reasoning = std::get_if<ReasoningBlock>(&block);
        if (!reasoning)
            continue;
        const QJsonValue encrypted = reasoning->providerOpaque.value(QStringLiteral("encrypted_content"));
        if (!encrypted.isString())
            continue; // without the encrypted blob it cannot be replayed
        input.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("reasoning")},
            {QStringLiteral("id"), reasoning->providerOpaque.value(QStringLiteral("id"))},
            {QStringLiteral("encrypted_content"), encrypted},
            {QStringLiteral("summary"), QJsonArray{}}});
    }
    if (const QString text = joinedText(message.blocks); !text.isEmpty())
        input.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                 {QStringLiteral("content"), text}});
    for (const ContentBlock &block : message.blocks)
        if (const auto *call = std::get_if<ToolCallBlock>(&block))
            input.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("function_call")},
                {QStringLiteral("call_id"), call->id},
                {QStringLiteral("name"), call->name},
                {QStringLiteral("arguments"),
                 QString::fromUtf8(QJsonDocument(call->args).toJson(QJsonDocument::Compact))}});
}

void appendToolResults(QJsonArray &input, const Message &message)
{
    // function_call_output carries text; any returned images are HOISTED onto a following user input
    // item as input_image parts (the same tool-result→user-block workaround as Chat Completions;
    // input_image on a user turn is well-defined, unlike images inside function_call_output).
    QList<ImageBlock> hoisted;
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
                hoisted.append(*image);
                hasImage = true;
            }
        }
        if (parts.isEmpty() && hasImage)
            parts.append(QStringLiteral("[see image below]"));
        if (hasImage)
            imageCallIds.append(result->callId);
        input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call_output")},
                                 {QStringLiteral("call_id"), result->callId},
                                 {QStringLiteral("output"), parts.join(QLatin1Char('\n'))}});
    }
    if (!hoisted.isEmpty()) {
        const QList<ContentBlock> note{ TextBlock{
            QStringLiteral("Images returned by tool call(s): %1").arg(imageCallIds.join(QStringLiteral(", "))) } };
        input.append(userInput(note, hoisted));
    }
}

QString effortString(ReasoningEffort effort)
{
    switch (effort) {
    case ReasoningEffort::Off: return {};
    case ReasoningEffort::Low: return QStringLiteral("low");
    case ReasoningEffort::Medium: return QStringLiteral("medium");
    case ReasoningEffort::High: return QStringLiteral("high");
    }
    return {};
}

void latchUsage(DecodedChunk &chunk, const QJsonObject &usage)
{
    if (usage.isEmpty())
        return;
    TokenUsage tokens;
    if (const QJsonValue v = usage.value(QStringLiteral("input_tokens")); v.isDouble())
        tokens.inputTokens = v.toInteger();
    if (const QJsonValue v = usage.value(QStringLiteral("output_tokens")); v.isDouble())
        tokens.outputTokens = v.toInteger();
    chunk.usage = tokens;
}

} // namespace

std::expected<QJsonObject, AgentError> encodeRequest(const InferenceRequest &request)
{
    // request.cacheStablePrefix is intentionally ignored: OpenAI Responses prompt
    // caching is automatic on a stable leading prefix (no wire marker exists).
    QStringList instructions;
    QJsonArray input;
    for (const Message &message : request.messages) {
        switch (message.role) {
        case Role::System:
            if (const QString text = joinedText(message.blocks); !text.isEmpty())
                instructions.append(text);
            break;
        case Role::User:
            input.append(userInput(message.blocks)); // content-array when the turn carries images
            break;
        case Role::Assistant:
            appendAssistant(input, message);
            break;
        case Role::Tool:
            appendToolResults(input, message);
            break;
        }
    }

    QJsonObject body{{QStringLiteral("model"), request.model},
                     {QStringLiteral("input"), input},
                     {QStringLiteral("stream"), true}};
    if (!instructions.isEmpty())
        body.insert(QStringLiteral("instructions"), instructions.join(QStringLiteral("\n\n")));
    if (!request.tools.isEmpty()) {
        QJsonArray tools;
        for (const ToolSpec &tool : request.tools)
            tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                     {QStringLiteral("name"), tool.name},
                                     {QStringLiteral("description"), tool.description},
                                     {QStringLiteral("parameters"), tool.inputSchema}});
        body.insert(QStringLiteral("tools"), tools);
    }
    if (request.temperature)
        body.insert(QStringLiteral("temperature"), *request.temperature);
    if (request.maxTokens)
        body.insert(QStringLiteral("max_output_tokens"), *request.maxTokens);
    if (const QString effort = effortString(request.reasoningEffort); !effort.isEmpty()) {
        body.insert(QStringLiteral("reasoning"),
                    QJsonObject{{QStringLiteral("effort"), effort}});
        // Stateless encrypted reasoning: we keep the transcript, the server does not.
        body.insert(QStringLiteral("store"), false);
        body.insert(QStringLiteral("include"),
                    QJsonArray{QStringLiteral("reasoning.encrypted_content")});
    }
    return body;
}

std::expected<DecodedChunk, AgentError> decodeEvent(const ServerSentEvent &event)
{
    DecodedChunk chunk;
    if (event.data.isEmpty())
        return chunk;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(event.data.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return std::unexpected(
            AgentError{AgentError::Code::Parse,
                       QStringLiteral("malformed Responses event: %1").arg(parseError.errorString()),
                       {}});
    const QJsonObject root = doc.object();
    const QString type = root.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("error") || type == QStringLiteral("response.failed")) {
        QString message = root.value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = root.value(QStringLiteral("response"))
                          .toObject()
                          .value(QStringLiteral("error"))
                          .toObject()
                          .value(QStringLiteral("message"))
                          .toString();
        return std::unexpected(AgentError{
            AgentError::Code::Provider,
            message.isEmpty() ? QStringLiteral("Responses stream error") : message, {}});
    }
    if (type == QStringLiteral("response.output_item.added")) {
        const QJsonObject item = root.value(QStringLiteral("item")).toObject();
        if (item.value(QStringLiteral("type")).toString() == QStringLiteral("function_call"))
            chunk.events.append(ToolCallStart{root.value(QStringLiteral("output_index")).toInt(),
                                              item.value(QStringLiteral("call_id")).toString(),
                                              item.value(QStringLiteral("name")).toString()});
        return chunk;
    }
    if (type == QStringLiteral("response.function_call_arguments.delta")) {
        chunk.events.append(ToolCallArgsDelta{root.value(QStringLiteral("output_index")).toInt(),
                                              root.value(QStringLiteral("delta")).toString()});
        return chunk;
    }
    if (type == QStringLiteral("response.output_text.delta")) {
        chunk.events.append(TextDelta{root.value(QStringLiteral("delta")).toString()});
        return chunk;
    }
    if (type == QStringLiteral("response.reasoning_summary_text.delta")
        || type == QStringLiteral("response.reasoning_text.delta")) {
        chunk.events.append(ReasoningDelta{root.value(QStringLiteral("delta")).toString(), std::nullopt});
        return chunk;
    }
    if (type == QStringLiteral("response.output_item.done")) {
        const QJsonObject item = root.value(QStringLiteral("item")).toObject();
        if (item.value(QStringLiteral("type")).toString() == QStringLiteral("reasoning")) {
            if (const QJsonValue enc = item.value(QStringLiteral("encrypted_content")); enc.isString())
                chunk.events.append(ReasoningDelta{
                    QString(),
                    QJsonObject{{QStringLiteral("id"), item.value(QStringLiteral("id"))},
                                {QStringLiteral("encrypted_content"), enc}}});
        }
        return chunk;
    }
    if (type == QStringLiteral("response.completed") || type == QStringLiteral("response.incomplete")) {
        const QJsonObject response = root.value(QStringLiteral("response")).toObject();
        latchUsage(chunk, response.value(QStringLiteral("usage")).toObject());
        if (response.value(QStringLiteral("status")).toString() == QStringLiteral("incomplete")) {
            const QString reason = response.value(QStringLiteral("incomplete_details"))
                                       .toObject()
                                       .value(QStringLiteral("reason"))
                                       .toString();
            chunk.stopReason = reason == QStringLiteral("max_output_tokens") ? StopReason::MaxTokens
                               : reason == QStringLiteral("content_filter")  ? StopReason::ContentFilter
                                                                             : StopReason::Other;
        } else {
            bool hasToolCall = false;
            for (const QJsonValue item : response.value(QStringLiteral("output")).toArray())
                if (item.toObject().value(QStringLiteral("type")).toString()
                    == QStringLiteral("function_call"))
                    hasToolCall = true;
            chunk.stopReason = hasToolCall ? StopReason::ToolCalls : StopReason::EndTurn;
        }
        return chunk;
    }
    // response.created / in_progress / *.done text / content_part.* / unknown: nothing.
    return chunk;
}

} // namespace karness::responses
