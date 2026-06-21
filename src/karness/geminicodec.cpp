// SPDX-License-Identifier: GPL-3.0-or-later
#include "geminicodec.h"

#include "reasoningbudget.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <variant>

namespace karness::gemini {

namespace {

QString joinedText(const QList<ContentBlock> &blocks)
{
    QStringList parts;
    for (const ContentBlock &block : blocks)
        if (const auto *text = std::get_if<TextBlock>(&block); text && !text->text.isEmpty())
            parts.append(text->text);
    return parts.join(QStringLiteral("\n\n"));
}

// A Gemini inlineData part for an image.
QJsonObject inlineDataPart(const ImageBlock &image)
{
    const QString mime = image.mimeType.isEmpty() ? QStringLiteral("image/jpeg") : image.mimeType;
    return QJsonObject{{QStringLiteral("inlineData"),
                        QJsonObject{{QStringLiteral("mimeType"), mime},
                                    {QStringLiteral("data"), QString::fromLatin1(image.data.toBase64())}}}};
}

// A user turn's parts: a {text} part (when non-empty) followed by an inlineData part per image.
QJsonArray userParts(const QList<ContentBlock> &blocks)
{
    QJsonArray parts;
    if (const QString text = joinedText(blocks); !text.isEmpty())
        parts.append(QJsonObject{{QStringLiteral("text"), text}});
    for (const ContentBlock &block : blocks)
        if (const auto *image = std::get_if<ImageBlock>(&block))
            parts.append(inlineDataPart(*image));
    return parts;
}

// A model (assistant) turn: text parts then functionCall parts.
QJsonObject modelContent(const Message &message)
{
    QJsonArray parts;
    for (const ContentBlock &block : message.blocks)
        if (const auto *text = std::get_if<TextBlock>(&block); text && !text->text.isEmpty())
            parts.append(QJsonObject{{QStringLiteral("text"), text->text}});
    for (const ContentBlock &block : message.blocks)
        if (const auto *call = std::get_if<ToolCallBlock>(&block))
            parts.append(QJsonObject{{QStringLiteral("functionCall"),
                                      QJsonObject{{QStringLiteral("name"), call->name},
                                                  {QStringLiteral("args"), call->args}}}});
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("model")},
                       {QStringLiteral("parts"), parts}};
}

// A tool turn -> a user content of functionResponse parts (keyed by name; the
// synthesized call id is the function name — Gemini has no call ids).
QJsonObject toolResponseContent(const Message &message)
{
    QJsonArray parts;
    for (const ContentBlock &block : message.blocks) {
        const auto *result = std::get_if<ToolResultBlock>(&block);
        if (!result)
            continue;
        QStringList text;
        QList<const ImageBlock *> images;
        for (const ContentPart &part : result->parts) {
            if (const auto *t = std::get_if<TextBlock>(&part); t && !t->text.isEmpty())
                text.append(t->text);
            else if (const auto *image = std::get_if<ImageBlock>(&part))
                images.append(image);
        }
        parts.append(QJsonObject{
            {QStringLiteral("functionResponse"),
             QJsonObject{{QStringLiteral("name"), result->callId},
                         {QStringLiteral("response"),
                          QJsonObject{{QStringLiteral("result"), text.join(QLatin1Char('\n'))}}}}}});
        // functionResponse carries no image; emit each returned image as a sibling inlineData part
        // in the same user content so a vision model still sees it.
        for (const ImageBlock *image : images)
            parts.append(inlineDataPart(*image));
    }
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                       {QStringLiteral("parts"), parts}};
}

} // namespace

std::expected<QJsonObject, AgentError> encodeRequest(const InferenceRequest &request)
{
    // request.cacheStablePrefix is intentionally ignored: Gemini implicit caching is
    // automatic on a stable prefix, and explicit context caching is a separate
    // CachedContent resource (own lifecycle/TTL) — out of scope here.
    QStringList systemParts;
    QJsonArray contents;
    for (const Message &message : request.messages) {
        switch (message.role) {
        case Role::System:
            if (const QString text = joinedText(message.blocks); !text.isEmpty())
                systemParts.append(text);
            break;
        case Role::User:
            contents.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                        {QStringLiteral("parts"), userParts(message.blocks)}});
            break;
        case Role::Assistant:
            contents.append(modelContent(message));
            break;
        case Role::Tool:
            contents.append(toolResponseContent(message));
            break;
        }
    }

    QJsonObject body{{QStringLiteral("contents"), contents}};
    if (!systemParts.isEmpty())
        body.insert(QStringLiteral("systemInstruction"),
                    QJsonObject{{QStringLiteral("parts"),
                                 QJsonArray{QJsonObject{{QStringLiteral("text"),
                                                         systemParts.join(QStringLiteral("\n\n"))}}}}});
    if (!request.tools.isEmpty()) {
        QJsonArray declarations;
        for (const ToolSpec &tool : request.tools)
            declarations.append(QJsonObject{{QStringLiteral("name"), tool.name},
                                            {QStringLiteral("description"), tool.description},
                                            {QStringLiteral("parameters"), tool.inputSchema}});
        body.insert(QStringLiteral("tools"),
                    QJsonArray{QJsonObject{{QStringLiteral("functionDeclarations"), declarations}}});
    }

    QJsonObject generationConfig;
    if (request.temperature)
        generationConfig.insert(QStringLiteral("temperature"), *request.temperature);
    if (request.maxTokens)
        generationConfig.insert(QStringLiteral("maxOutputTokens"), *request.maxTokens);
    if (const int budget = thinkingBudgetTokens(request.reasoningEffort); budget > 0)
        generationConfig.insert(QStringLiteral("thinkingConfig"),
                                QJsonObject{{QStringLiteral("thinkingBudget"), budget},
                                            {QStringLiteral("includeThoughts"), true}});
    if (!generationConfig.isEmpty())
        body.insert(QStringLiteral("generationConfig"), generationConfig);
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
                       QStringLiteral("malformed Gemini chunk: %1").arg(parseError.errorString()),
                       {}});
    const QJsonObject root = doc.object();

    if (const QJsonValue error = root.value(QStringLiteral("error")); error.isObject())
        return std::unexpected(AgentError{
            AgentError::Code::Provider,
            error.toObject().value(QStringLiteral("message")).toString(QStringLiteral("Gemini error")),
            {}});

    if (const QJsonValue usage = root.value(QStringLiteral("usageMetadata")); usage.isObject()) {
        TokenUsage tokens;
        const QJsonObject object = usage.toObject();
        if (const QJsonValue v = object.value(QStringLiteral("promptTokenCount")); v.isDouble())
            tokens.inputTokens = v.toInteger();
        if (const QJsonValue v = object.value(QStringLiteral("candidatesTokenCount")); v.isDouble())
            tokens.outputTokens = v.toInteger();
        chunk.usage = tokens;
    }

    const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty())
        return chunk;
    const QJsonObject candidate = candidates.first().toObject();

    int toolIndex = 0;
    bool sawToolCall = false;
    const QJsonArray parts =
        candidate.value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray();
    for (const QJsonValue partValue : parts) {
        const QJsonObject part = partValue.toObject();
        if (const QJsonValue call = part.value(QStringLiteral("functionCall")); call.isObject()) {
            const QJsonObject function = call.toObject();
            const QString name = function.value(QStringLiteral("name")).toString();
            chunk.events.append(ToolCallStart{toolIndex, name, name}); // no call id -> name is the id
            chunk.events.append(ToolCallArgsDelta{
                toolIndex, QString::fromUtf8(QJsonDocument(function.value(QStringLiteral("args"))
                                                               .toObject())
                                                 .toJson(QJsonDocument::Compact))});
            ++toolIndex;
            sawToolCall = true;
        } else if (const QJsonValue text = part.value(QStringLiteral("text")); text.isString()) {
            if (part.value(QStringLiteral("thought")).toBool()) {
                std::optional<QJsonObject> opaque;
                if (const QJsonValue sig = part.value(QStringLiteral("thoughtSignature")); sig.isString())
                    opaque = QJsonObject{{QStringLiteral("thoughtSignature"), sig}};
                chunk.events.append(ReasoningDelta{text.toString(), opaque});
            } else {
                chunk.events.append(TextDelta{text.toString()});
            }
        } else if (const QJsonValue sig = part.value(QStringLiteral("thoughtSignature"));
                   sig.isString()) {
            chunk.events.append(
                ReasoningDelta{QString(), QJsonObject{{QStringLiteral("thoughtSignature"), sig}}});
        }
    }

    if (const QJsonValue finish = candidate.value(QStringLiteral("finishReason")); finish.isString())
        chunk.stopReason = sawToolCall ? StopReason::ToolCalls : mapFinishReason(finish.toString());
    else if (sawToolCall)
        chunk.stopReason = StopReason::ToolCalls; // calls without an explicit finishReason
    return chunk;
}

StopReason mapFinishReason(const QString &finishReason)
{
    if (finishReason == QStringLiteral("STOP"))
        return StopReason::EndTurn;
    if (finishReason == QStringLiteral("MAX_TOKENS"))
        return StopReason::MaxTokens;
    if (finishReason == QStringLiteral("SAFETY") || finishReason == QStringLiteral("RECITATION")
        || finishReason == QStringLiteral("BLOCKLIST")
        || finishReason == QStringLiteral("PROHIBITED_CONTENT")
        || finishReason == QStringLiteral("SPII"))
        return StopReason::ContentFilter;
    return StopReason::Other;
}

} // namespace karness::gemini
