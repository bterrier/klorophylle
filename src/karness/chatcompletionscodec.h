// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agenterror.h"
#include "decodedchunk.h"
#include "inferencerequest.h"
#include "streamevent.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>

#include <expected>

namespace karness::chatcompletions {

// Pure encode/decode for the OpenAI Chat Completions dialect (docs/adr/0019
// decision 3a) — the compat dialect that also covers Ollama, llama.cpp
// server, LM Studio, vLLM, OpenRouter and any BYOK endpoint. The transport
// (openaicompatprovider.h) owns sockets and the [DONE] sentinel; these
// functions only map JSON.

// Request body for POST {baseUrl}/chat/completions. Always streams
// (stream:true + stream_options.include_usage). Optional request fields are
// omitted when unset, tools is omitted when empty (some compat servers 400
// on an empty array), reasoningEffort != Off becomes "reasoning_effort".
// Canonical-message mapping: System/User text joins to a plain string
// content (content-part arrays arrive with vision support); assistant
// ToolCallBlocks become "tool_calls" with compact-JSON argument strings;
// a Tool message yields one {"role":"tool"} wire message per
// ToolResultBlock. ReasoningBlocks are dropped — Chat Completions has no
// slot for echoing reasoning, and compat reasoning carries no signatures
// (ADR 0019 decision 6: fidelity only where providers verify it). Any
// ImageBlock is an error until vision plumbing lands — never silently
// dropped.
[[nodiscard]] std::expected<QJsonObject, AgentError> encodeRequest(const InferenceRequest &request);

// Decodes one SSE data payload (never "[DONE]" — the transport filters it)
// into the shared DecodedChunk: the transport feeds the events onward and
// latches stopReason/usage for the Done it builds at stream end (OpenAI sends
// usage in a final chunk whose "choices" is empty).
// delta.content -> TextDelta; delta.reasoning_content (DeepSeek) or
// delta.reasoning (OpenRouter) -> ReasoningDelta; delta.tool_calls entries
// with a function.name -> ToolCallStart (plus ToolCallArgsDelta when the
// same entry already carries arguments — Ollama sends complete calls in one
// chunk), name-less entries -> ToolCallArgsDelta; a missing "index" falls
// back to the entry's position. Unknown fields (system_fingerprint,
// llama.cpp's timings, ...) are ignored. Malformed JSON -> Code::Parse.
[[nodiscard]] std::expected<DecodedChunk, AgentError> decodeChunk(const QByteArray &data);

// "stop"->EndTurn, "tool_calls"/"function_call"->ToolCalls,
// "length"->MaxTokens, "content_filter"->ContentFilter, anything else->Other.
[[nodiscard]] StopReason mapFinishReason(const QString &finishReason);

} // namespace karness::chatcompletions
