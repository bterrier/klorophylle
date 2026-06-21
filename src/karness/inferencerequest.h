// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "message.h"
#include "toolspec.h"

#include <optional>

namespace karness {

// Normalized reasoning knob; dialects map it to each provider's control
// (OpenAI reasoning.effort, Anthropic/Gemini thinking budgets) — docs/adr/0019
// decision 6.
enum class ReasoningEffort { Off, Low, Medium, High };

// One provider turn. System prompts travel as a Role::System message;
// dialects hoist them where the wire format keeps them separate. Optional
// fields are OMITTED from the wire when unset (some providers reject
// temperature on reasoning models — unset must be representable).
struct InferenceRequest {
    QString model;
    QList<Message> messages;
    QList<ToolSpec> tools;
    ReasoningEffort reasoningEffort = ReasoningEffort::Off;
    std::optional<double> temperature;
    std::optional<quint32> seed; // deterministic runs where supported
    std::optional<int> maxTokens;
    // Hint that the leading system + tools form a STABLE cacheable prefix
    // (docs/adr/0019 cache-placement follow-up). Each dialect self-interprets:
    // the Anthropic codec emits an explicit cache_control: {type:"ephemeral"}
    // breakpoint; OpenAI/compat caching is automatic and Gemini's is implicit,
    // so those codecs ignore it. The session sets it once the prefix is stable.
    bool cacheStablePrefix = false;

    bool operator==(const InferenceRequest &) const = default;
};

// The caller-set generation knobs for a session (docs/adr/0019 cache-placement
// follow-up). Distinct from InferenceRequest: a session owns messages/tools and
// assembles the full request itself, so the host only supplies these. Mirrors the
// knob fields of InferenceRequest; buildRequest copies them onto each turn's request.
struct ModelConfig {
    QString model;
    ReasoningEffort reasoningEffort = ReasoningEffort::Off;
    std::optional<double> temperature;
    std::optional<quint32> seed;
    std::optional<int> maxTokens;

    bool operator==(const ModelConfig &) const = default;
};

} // namespace karness
