// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agenterror.h"
#include "message.h"

#include <QtCore/QJsonObject>

#include <optional>
#include <variant>

namespace karness {

// Typed streaming events emitted by a provider turn (docs/adr/0019 decision 3).
// Dialects decode their wire stream (SSE etc.) into these; consumers receive
// them via QFuture<StreamEvent> (one addResult per event).

struct TextDelta {
    QString text;

    bool operator==(const TextDelta &) const = default;
};

struct ReasoningDelta {
    QString text;
    // The dialect's opaque echo blob for the reasoning block being streamed
    // (Anthropic thinking `signature`, OpenAI Responses reasoning item id +
    // `encrypted_content`, Gemini `thoughtSignature`). Usually arrives on a
    // trailing frame with empty text; the accumulator merges it into the open
    // reasoning block so the Done message round-trips it verbatim through a
    // tool loop (docs/adr/0019 decision 2). Empty for the compat dialect.
    std::optional<QJsonObject> providerOpaque;

    bool operator==(const ReasoningDelta &) const = default;
};

// A tool call opened by the model; later args deltas reference it by index
// (providers stream parallel calls interleaved, keyed by index).
struct ToolCallStart {
    int index = 0;
    QString id;
    QString name;

    bool operator==(const ToolCallStart &) const = default;
};

struct ToolCallArgsDelta {
    int index = 0;
    QString argsDelta; // raw JSON fragment; parsed once the turn completes

    bool operator==(const ToolCallArgsDelta &) const = default;
};

struct TokenUsage {
    std::optional<qint64> inputTokens;
    std::optional<qint64> outputTokens;

    bool operator==(const TokenUsage &) const = default;
};

// Normalized stop reason — the agent loop branches on this (run tools and
// loop vs. turn over); dialects map each provider's spelling, never the UI.
enum class StopReason { EndTurn, ToolCalls, MaxTokens, ContentFilter, Other };

// Terminal success event: the fully assembled assistant message.
struct Done {
    Message message;
    StopReason stopReason = StopReason::EndTurn;
    std::optional<TokenUsage> usage;

    bool operator==(const Done &) const = default;
};

// Terminal failure event (the documented QFuture idiom — providers never
// use QPromise::setException; see iprovider.h).
struct ErrorEvent {
    AgentError error;

    bool operator==(const ErrorEvent &) const = default;
};

using StreamEvent =
    std::variant<TextDelta, ReasoningDelta, ToolCallStart, ToolCallArgsDelta, Done, ErrorEvent>;

// QFuture<T> documents that T needs default + copy constructors. The default
// ctor comes from the FIRST variant alternative — keep it default-constructible
// (reordering the variant can silently break QFuture<StreamEvent>).
static_assert(std::is_default_constructible_v<StreamEvent>);
static_assert(std::is_copy_constructible_v<StreamEvent>);

} // namespace karness
