// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agenterror.h"
#include "decodedchunk.h"
#include "inferencerequest.h"
#include "sseparser.h"

#include <QtCore/QJsonObject>

#include <expected>

namespace karness::anthropic {

// Pure encode/decode for the Anthropic Messages dialect (docs/adr/0019
// decision 3c). The transport (anthropicprovider / StreamingProvider) owns
// sockets, headers and termination (the `message_stop` event); these map JSON.

// Output budget used when the request leaves maxTokens unset; Anthropic
// REQUIRES max_tokens, and it must exceed any thinking budget_tokens.
constexpr int kDefaultMaxTokens = 1024;

// Request body for POST {baseUrl}/messages. System messages are hoisted to the
// top-level `system`; user/assistant/tool messages become content-block arrays
// (text/thinking/tool_use; a Role::Tool message becomes a user message of
// tool_result blocks — Anthropic carries results on the user turn). Assistant
// ReasoningBlocks re-encode as `thinking` with their `signature` from
// providerOpaque (echoed verbatim so a tool loop validates). reasoningEffort
// maps to `thinking.budget_tokens`; temperature is dropped when thinking is on
// (Anthropic rejects the pair). Any ImageBlock is an error until vision support.
[[nodiscard]] std::expected<QJsonObject, AgentError> encodeRequest(const InferenceRequest &request);

// Decodes one Anthropic SSE event. message_start latches input tokens;
// content_block_start(tool_use) -> ToolCallStart; content_block_delta ->
// TextDelta / ReasoningDelta (thinking_delta, and signature_delta as an
// opaque-only ReasoningDelta) / ToolCallArgsDelta (input_json_delta);
// message_delta latches stop_reason + output tokens. ping / content_block_stop
// / message_stop carry nothing. An `error` event becomes a Provider error.
[[nodiscard]] std::expected<DecodedChunk, AgentError> decodeEvent(const ServerSentEvent &event);

// "end_turn"/"stop_sequence"->EndTurn, "tool_use"->ToolCalls,
// "max_tokens"->MaxTokens, "refusal"->ContentFilter, anything else->Other.
[[nodiscard]] StopReason mapStopReason(const QString &stopReason);

} // namespace karness::anthropic
