// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>

#include <variant>

namespace karness {

// Canonical, provider-neutral message model (docs/adr/0019 decision 2).
// Dialects encode/decode this to each provider's wire format; the
// provider-opaque payload on ReasoningBlock round-trips verbatim so
// signature-bearing reasoning (Anthropic thinking, OpenAI Responses
// encrypted items) survives a tool loop.

enum class Role { System, User, Assistant, Tool };

struct TextBlock {
    QString text;

    bool operator==(const TextBlock &) const = default;
};

struct ImageBlock {
    QByteArray data; // raw bytes (base64 only at the JSON boundary)
    QString mimeType;

    bool operator==(const ImageBlock &) const = default;
};

struct ReasoningBlock {
    QString text;
    QJsonObject providerOpaque; // dialect-owned, echoed back verbatim

    bool operator==(const ReasoningBlock &) const = default;
};

struct ToolCallBlock {
    QString id;
    QString name;
    QJsonObject args;

    bool operator==(const ToolCallBlock &) const = default;
};

// Tool results carry text/image parts only — a deliberate narrowing of
// ADR 0019 decision 2's QList<ContentBlock> (avoids a recursive variant;
// no provider accepts reasoning or nested tool calls inside a tool result).
using ContentPart = std::variant<TextBlock, ImageBlock>;

struct ToolResultBlock {
    QString callId; // matches ToolCallBlock::id
    QList<ContentPart> parts;
    bool isError = false;

    bool operator==(const ToolResultBlock &) const = default;
};

using ContentBlock =
    std::variant<TextBlock, ReasoningBlock, ToolCallBlock, ToolResultBlock, ImageBlock>;

struct Message {
    Role role = Role::User;
    QList<ContentBlock> blocks;

    bool operator==(const Message &) const = default;
};

} // namespace karness
