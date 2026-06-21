// SPDX-License-Identifier: GPL-3.0-or-later
#include "messagecodec.h"

#include <QtCore/QJsonArray>

namespace karness {

namespace {

QString roleToString(Role role)
{
    switch (role) {
    case Role::System: return QStringLiteral("system");
    case Role::User: return QStringLiteral("user");
    case Role::Assistant: return QStringLiteral("assistant");
    case Role::Tool: return QStringLiteral("tool");
    }
    Q_UNREACHABLE();
}

std::expected<Role, MessageCodecError> roleFromString(const QString &role)
{
    if (role == QStringLiteral("system")) return Role::System;
    if (role == QStringLiteral("user")) return Role::User;
    if (role == QStringLiteral("assistant")) return Role::Assistant;
    if (role == QStringLiteral("tool")) return Role::Tool;
    return std::unexpected(MessageCodecError::UnknownRole);
}

QJsonObject textToJson(const TextBlock &block)
{
    return {{QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), block.text}};
}

QJsonObject imageToJson(const ImageBlock &block)
{
    return {{QStringLiteral("type"), QStringLiteral("image")},
            {QStringLiteral("mimeType"), block.mimeType},
            {QStringLiteral("data"), QString::fromLatin1(block.data.toBase64())}};
}

QJsonObject blockToJson(const ContentBlock &block)
{
    return std::visit(
        [](const auto &b) -> QJsonObject {
            using T = std::decay_t<decltype(b)>;
            if constexpr (std::is_same_v<T, TextBlock>) {
                return textToJson(b);
            } else if constexpr (std::is_same_v<T, ReasoningBlock>) {
                QJsonObject json{{QStringLiteral("type"), QStringLiteral("reasoning")},
                                 {QStringLiteral("text"), b.text}};
                if (!b.providerOpaque.isEmpty())
                    json.insert(QStringLiteral("providerOpaque"), b.providerOpaque);
                return json;
            } else if constexpr (std::is_same_v<T, ToolCallBlock>) {
                return {{QStringLiteral("type"), QStringLiteral("tool_call")},
                        {QStringLiteral("id"), b.id},
                        {QStringLiteral("name"), b.name},
                        {QStringLiteral("args"), b.args}};
            } else if constexpr (std::is_same_v<T, ToolResultBlock>) {
                QJsonArray parts;
                for (const ContentPart &part : b.parts)
                    parts.append(std::visit(
                        [](const auto &p) -> QJsonObject {
                            using P = std::decay_t<decltype(p)>;
                            if constexpr (std::is_same_v<P, TextBlock>)
                                return textToJson(p);
                            else
                                return imageToJson(p);
                        },
                        part));
                return {{QStringLiteral("type"), QStringLiteral("tool_result")},
                        {QStringLiteral("callId"), b.callId},
                        {QStringLiteral("isError"), b.isError},
                        {QStringLiteral("parts"), parts}};
            } else {
                static_assert(std::is_same_v<T, ImageBlock>);
                return imageToJson(b);
            }
        },
        block);
}

// Required-field accessor: present and a string, else MissingField.
std::expected<QString, MessageCodecError> requireString(const QJsonObject &json, QLatin1StringView key)
{
    const QJsonValue value = json.value(key);
    if (!value.isString())
        return std::unexpected(MessageCodecError::MissingField);
    return value.toString();
}

std::expected<TextBlock, MessageCodecError> textFromJson(const QJsonObject &json)
{
    return requireString(json, QLatin1StringView("text"))
        .transform([](QString text) { return TextBlock{std::move(text)}; });
}

std::expected<ImageBlock, MessageCodecError> imageFromJson(const QJsonObject &json)
{
    const auto mimeType = requireString(json, QLatin1StringView("mimeType"));
    if (!mimeType)
        return std::unexpected(mimeType.error());
    const auto base64 = requireString(json, QLatin1StringView("data"));
    if (!base64)
        return std::unexpected(base64.error());
    const auto decoded = QByteArray::fromBase64Encoding(base64->toLatin1(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return std::unexpected(MessageCodecError::InvalidBase64);
    return ImageBlock{*decoded, *mimeType};
}

std::expected<ContentBlock, MessageCodecError> blockFromJson(const QJsonObject &json)
{
    const auto type = requireString(json, QLatin1StringView("type"));
    if (!type)
        return std::unexpected(type.error());

    if (*type == QStringLiteral("text"))
        return textFromJson(json);

    if (*type == QStringLiteral("image"))
        return imageFromJson(json);

    if (*type == QStringLiteral("reasoning")) {
        const auto text = requireString(json, QLatin1StringView("text"));
        if (!text)
            return std::unexpected(text.error());
        return ReasoningBlock{*text,
                              json.value(QLatin1StringView("providerOpaque")).toObject()};
    }

    if (*type == QStringLiteral("tool_call")) {
        const auto id = requireString(json, QLatin1StringView("id"));
        if (!id)
            return std::unexpected(id.error());
        const auto name = requireString(json, QLatin1StringView("name"));
        if (!name)
            return std::unexpected(name.error());
        const QJsonValue args = json.value(QLatin1StringView("args"));
        if (!args.isObject())
            return std::unexpected(MessageCodecError::MissingField);
        return ToolCallBlock{*id, *name, args.toObject()};
    }

    if (*type == QStringLiteral("tool_result")) {
        const auto callId = requireString(json, QLatin1StringView("callId"));
        if (!callId)
            return std::unexpected(callId.error());
        const QJsonValue partsValue = json.value(QLatin1StringView("parts"));
        if (!partsValue.isArray())
            return std::unexpected(MessageCodecError::MissingField);
        QList<ContentPart> parts;
        for (const QJsonValue &partValue : partsValue.toArray()) {
            const QJsonObject partJson = partValue.toObject();
            const auto partType = requireString(partJson, QLatin1StringView("type"));
            if (!partType)
                return std::unexpected(partType.error());
            if (*partType == QStringLiteral("text")) {
                const auto text = textFromJson(partJson);
                if (!text)
                    return std::unexpected(text.error());
                parts.append(*text);
            } else if (*partType == QStringLiteral("image")) {
                const auto image = imageFromJson(partJson);
                if (!image)
                    return std::unexpected(image.error());
                parts.append(*image);
            } else {
                return std::unexpected(MessageCodecError::UnknownBlockType);
            }
        }
        return ToolResultBlock{*callId, std::move(parts),
                               json.value(QLatin1StringView("isError")).toBool()};
    }

    return std::unexpected(MessageCodecError::UnknownBlockType);
}

} // namespace

QJsonObject messageToJson(const Message &message)
{
    QJsonArray blocks;
    for (const ContentBlock &block : message.blocks)
        blocks.append(blockToJson(block));
    return {{QStringLiteral("role"), roleToString(message.role)},
            {QStringLiteral("blocks"), blocks}};
}

std::expected<Message, MessageCodecError> messageFromJson(const QJsonObject &json)
{
    if (json.isEmpty())
        return std::unexpected(MessageCodecError::NotAnObject);

    const auto roleString = requireString(json, QLatin1StringView("role"));
    if (!roleString)
        return std::unexpected(roleString.error());
    const auto role = roleFromString(*roleString);
    if (!role)
        return std::unexpected(role.error());

    const QJsonValue blocksValue = json.value(QLatin1StringView("blocks"));
    if (!blocksValue.isArray())
        return std::unexpected(MessageCodecError::MissingField);

    Message message{*role, {}};
    for (const QJsonValue &blockValue : blocksValue.toArray()) {
        if (!blockValue.isObject())
            return std::unexpected(MessageCodecError::UnknownBlockType);
        auto block = blockFromJson(blockValue.toObject());
        if (!block)
            return std::unexpected(block.error());
        message.blocks.append(std::move(*block));
    }
    return message;
}

} // namespace karness
