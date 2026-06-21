// SPDX-License-Identifier: GPL-3.0-or-later
#include "anthropiccodec.h"

#include "reasoningbudget.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <variant>

namespace karness::anthropic {

namespace {

QString joinedText(const QList<ContentBlock> &blocks)
{
    QStringList parts;
    for (const ContentBlock &block : blocks)
        if (const auto *text = std::get_if<TextBlock>(&block); text && !text->text.isEmpty())
            parts.append(text->text);
    return parts.join(QStringLiteral("\n\n"));
}

// An Anthropic image content block: source.type "base64". Valid both as a top-level user block
// and inside a tool_result's content array.
QJsonObject imageBlock(const ImageBlock &image)
{
    const QString mime = image.mimeType.isEmpty() ? QStringLiteral("image/jpeg") : image.mimeType;
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("image")},
        {QStringLiteral("source"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                     {QStringLiteral("media_type"), mime},
                     {QStringLiteral("data"), QString::fromLatin1(image.data.toBase64())}}}};
}

// A user turn: text + image content blocks (tool results travel as Role::Tool).
QJsonObject userMessage(const Message &message)
{
    QJsonArray content;
    for (const ContentBlock &block : message.blocks) {
        if (const auto *text = std::get_if<TextBlock>(&block))
            content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                       {QStringLiteral("text"), text->text}});
        else if (const auto *image = std::get_if<ImageBlock>(&block))
            content.append(imageBlock(*image));
    }
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                       {QStringLiteral("content"), content}};
}

// An assistant turn: thinking (with its echoed signature), text and tool_use
// blocks, in arrival order — thinking first, as Anthropic requires.
QJsonObject assistantMessage(const Message &message)
{
    QJsonArray content;
    for (const ContentBlock &block : message.blocks) {
        if (const auto *text = std::get_if<TextBlock>(&block)) {
            if (!text->text.isEmpty())
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                           {QStringLiteral("text"), text->text}});
        } else if (const auto *reasoning = std::get_if<ReasoningBlock>(&block)) {
            QJsonObject thinking{{QStringLiteral("type"), QStringLiteral("thinking")},
                                 {QStringLiteral("thinking"), reasoning->text}};
            if (const QJsonValue sig = reasoning->providerOpaque.value(QStringLiteral("signature"));
                sig.isString())
                thinking.insert(QStringLiteral("signature"), sig);
            content.append(thinking);
        } else if (const auto *call = std::get_if<ToolCallBlock>(&block)) {
            content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                       {QStringLiteral("id"), call->id},
                                       {QStringLiteral("name"), call->name},
                                       {QStringLiteral("input"), call->args}});
        }
    }
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                       {QStringLiteral("content"), content}};
}

// Anthropic carries tool results on a USER turn as tool_result blocks.
QJsonObject toolResultMessage(const Message &message)
{
    QJsonArray content;
    for (const ContentBlock &block : message.blocks) {
        const auto *result = std::get_if<ToolResultBlock>(&block);
        if (!result)
            continue;
        QJsonArray parts;
        for (const ContentPart &part : result->parts) {
            if (const auto *text = std::get_if<TextBlock>(&part))
                parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                         {QStringLiteral("text"), text->text}});
            else if (const auto *image = std::get_if<ImageBlock>(&part))
                parts.append(imageBlock(*image)); // Anthropic allows images inside tool_result
        }
        QJsonObject toolResult{{QStringLiteral("type"), QStringLiteral("tool_result")},
                               {QStringLiteral("tool_use_id"), result->callId},
                               {QStringLiteral("content"), parts}};
        if (result->isError)
            toolResult.insert(QStringLiteral("is_error"), true);
        content.append(toolResult);
    }
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                       {QStringLiteral("content"), content}};
}

} // namespace

std::expected<QJsonObject, AgentError> encodeRequest(const InferenceRequest &request)
{
    QStringList systemParts;
    QJsonArray messages;
    for (const Message &message : request.messages) {
        switch (message.role) {
        case Role::System:
            if (const QString text = joinedText(message.blocks); !text.isEmpty())
                systemParts.append(text);
            break;
        case Role::User:
            messages.append(userMessage(message));
            break;
        case Role::Assistant:
            messages.append(assistantMessage(message));
            break;
        case Role::Tool:
            messages.append(toolResultMessage(message));
            break;
        }
    }

    const int budget = thinkingBudgetTokens(request.reasoningEffort);
    const bool thinking = budget > 0;
    int maxTokens = request.maxTokens.value_or(kDefaultMaxTokens);
    if (thinking && maxTokens <= budget) // max_tokens must exceed the thinking budget
        maxTokens = budget + kDefaultMaxTokens;

    QJsonObject body{{QStringLiteral("model"), request.model},
                     {QStringLiteral("max_tokens"), maxTokens},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("messages"), messages}};
    // Anthropic prompt caching is an explicit inline breakpoint. Render order is
    // tools -> system -> messages, so a cache_control marker on the system block
    // caches tools + system together (one breakpoint, max 4). When there is no
    // system block we fall back to marking the last tool. Prefixes below
    // ~2048/4096 tokens (model-dependent) are silently not cached — harmless.
    const QJsonObject kEphemeral{{QStringLiteral("cache_control"),
                                  QJsonObject{{QStringLiteral("type"),
                                               QStringLiteral("ephemeral")}}}};
    const bool markSystem = request.cacheStablePrefix && !systemParts.isEmpty();
    if (!systemParts.isEmpty()) {
        const QString joined = systemParts.join(QStringLiteral("\n\n"));
        if (markSystem) {
            QJsonObject block{{QStringLiteral("type"), QStringLiteral("text")},
                              {QStringLiteral("text"), joined}};
            block.insert(QStringLiteral("cache_control"),
                         kEphemeral.value(QStringLiteral("cache_control")));
            body.insert(QStringLiteral("system"), QJsonArray{block});
        } else {
            body.insert(QStringLiteral("system"), joined);
        }
    }
    if (!request.tools.isEmpty()) {
        QJsonArray tools;
        for (const ToolSpec &tool : request.tools)
            tools.append(QJsonObject{{QStringLiteral("name"), tool.name},
                                     {QStringLiteral("description"), tool.description},
                                     {QStringLiteral("input_schema"), tool.inputSchema}});
        // Only needed when no system block carried the breakpoint (system caches tools too).
        if (request.cacheStablePrefix && !markSystem) {
            QJsonObject last = tools.last().toObject();
            last.insert(QStringLiteral("cache_control"),
                        kEphemeral.value(QStringLiteral("cache_control")));
            tools.replace(tools.size() - 1, last);
        }
        body.insert(QStringLiteral("tools"), tools);
    }
    if (thinking)
        body.insert(QStringLiteral("thinking"),
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("enabled")},
                                {QStringLiteral("budget_tokens"), budget}});
    // Anthropic rejects temperature alongside extended thinking; otherwise pass it through.
    if (request.temperature && !thinking)
        body.insert(QStringLiteral("temperature"), *request.temperature);
    return body;
}

std::expected<DecodedChunk, AgentError> decodeEvent(const ServerSentEvent &event)
{
    DecodedChunk chunk;
    if (event.data.isEmpty()) // a bare event: line (no data) carries nothing
        return chunk;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(event.data.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return std::unexpected(
            AgentError{AgentError::Code::Parse,
                       QStringLiteral("malformed Anthropic event: %1").arg(parseError.errorString()),
                       {}});
    const QJsonObject root = doc.object();
    const QString type = root.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("error")) {
        const QString message = root.value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toString();
        return std::unexpected(AgentError{
            AgentError::Code::Provider,
            message.isEmpty() ? QStringLiteral("Anthropic stream error") : message, {}});
    }
    if (type == QStringLiteral("message_start")) {
        const QJsonObject usage =
            root.value(QStringLiteral("message")).toObject().value(QStringLiteral("usage")).toObject();
        if (const QJsonValue v = usage.value(QStringLiteral("input_tokens")); v.isDouble()) {
            TokenUsage tokens;
            tokens.inputTokens = v.toInteger();
            chunk.usage = tokens;
        }
        return chunk;
    }
    if (type == QStringLiteral("content_block_start")) {
        const int index = root.value(QStringLiteral("index")).toInt();
        const QJsonObject block = root.value(QStringLiteral("content_block")).toObject();
        if (block.value(QStringLiteral("type")).toString() == QStringLiteral("tool_use"))
            chunk.events.append(ToolCallStart{index, block.value(QStringLiteral("id")).toString(),
                                              block.value(QStringLiteral("name")).toString()});
        return chunk; // text / thinking: content arrives in the deltas
    }
    if (type == QStringLiteral("content_block_delta")) {
        const int index = root.value(QStringLiteral("index")).toInt();
        const QJsonObject delta = root.value(QStringLiteral("delta")).toObject();
        const QString deltaType = delta.value(QStringLiteral("type")).toString();
        if (deltaType == QStringLiteral("text_delta")) {
            chunk.events.append(TextDelta{delta.value(QStringLiteral("text")).toString()});
        } else if (deltaType == QStringLiteral("thinking_delta")) {
            chunk.events.append(
                ReasoningDelta{delta.value(QStringLiteral("thinking")).toString(), std::nullopt});
        } else if (deltaType == QStringLiteral("signature_delta")) {
            chunk.events.append(ReasoningDelta{
                QString(),
                QJsonObject{{QStringLiteral("signature"), delta.value(QStringLiteral("signature"))}}});
        } else if (deltaType == QStringLiteral("input_json_delta")) {
            chunk.events.append(
                ToolCallArgsDelta{index, delta.value(QStringLiteral("partial_json")).toString()});
        }
        return chunk;
    }
    if (type == QStringLiteral("message_delta")) {
        const QJsonObject delta = root.value(QStringLiteral("delta")).toObject();
        if (const QJsonValue stop = delta.value(QStringLiteral("stop_reason")); stop.isString())
            chunk.stopReason = mapStopReason(stop.toString());
        const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
        if (const QJsonValue v = usage.value(QStringLiteral("output_tokens")); v.isDouble()) {
            TokenUsage tokens;
            tokens.outputTokens = v.toInteger();
            chunk.usage = tokens;
        }
        return chunk;
    }
    // ping / content_block_stop / message_stop / unknown: nothing to forward.
    return chunk;
}

StopReason mapStopReason(const QString &stopReason)
{
    if (stopReason == QStringLiteral("end_turn") || stopReason == QStringLiteral("stop_sequence"))
        return StopReason::EndTurn;
    if (stopReason == QStringLiteral("tool_use"))
        return StopReason::ToolCalls;
    if (stopReason == QStringLiteral("max_tokens"))
        return StopReason::MaxTokens;
    if (stopReason == QStringLiteral("refusal"))
        return StopReason::ContentFilter;
    return StopReason::Other;
}

} // namespace karness::anthropic
