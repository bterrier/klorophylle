// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agenterror.h"
#include "decodedchunk.h"
#include "inferencerequest.h"
#include "sseparser.h"

#include <QtCore/QJsonObject>

#include <expected>

namespace karness::responses {

// Pure encode/decode for the OpenAI Responses dialect (docs/adr/0019
// decision 3b). The transport (responsesprovider / StreamingProvider) owns
// sockets and termination (the stream closes after response.completed — no
// "[DONE]"); these functions only map JSON.

// Request body for POST {baseUrl}/responses. System messages hoist to
// `instructions`; user/assistant/tool messages become typed `input` items
// (message / function_call / function_call_output / reasoning). Tools are FLAT
// ({type:function, name, description, parameters} — not the Chat Completions
// nesting). reasoningEffort maps to reasoning.effort; when reasoning is on the
// request also opts into stateless encrypted reasoning (store:false +
// include:[reasoning.encrypted_content]) so the opaque item round-trips a tool
// loop. Assistant ReasoningBlocks re-encode as `reasoning` items carrying their
// providerOpaque id + encrypted_content. Any ImageBlock is an error until vision support.
[[nodiscard]] std::expected<QJsonObject, AgentError> encodeRequest(const InferenceRequest &request);

// Decodes one Responses SSE event. output_item.added(function_call) ->
// ToolCallStart; function_call_arguments.delta -> ToolCallArgsDelta;
// output_text.delta -> TextDelta; reasoning_summary_text.delta /
// reasoning_text.delta -> ReasoningDelta; output_item.done(reasoning) ->
// an opaque-only ReasoningDelta (id + encrypted_content); completed /
// incomplete latch usage + the stop reason; failed / error -> Provider error.
[[nodiscard]] std::expected<DecodedChunk, AgentError> decodeEvent(const ServerSentEvent &event);

} // namespace karness::responses
