// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agenterror.h"
#include "decodedchunk.h"
#include "inferencerequest.h"
#include "sseparser.h"

#include <QtCore/QJsonObject>

#include <expected>

namespace karness::gemini {

// Pure encode/decode for the Gemini generateContent dialect (docs/adr/0019
// decision 3d). The transport (geminiprovider / StreamingProvider) owns
// sockets, the x-goog-api-key header and the :streamGenerateContent?alt=sse
// endpoint; these functions only map JSON.

// Request body. System messages -> systemInstruction; user/assistant/tool
// messages -> `contents` (role user|model, parts text|functionCall|
// functionResponse). Tools are wrapped in one functionDeclarations entry.
// reasoningEffort -> generationConfig.thinkingConfig.thinkingBudget (+
// includeThoughts). Gemini function calls have no id, so encoding a tool
// result keys its functionResponse by the call's name (the synthesized id).
// Any ImageBlock is an error until vision support.
[[nodiscard]] std::expected<QJsonObject, AgentError> encodeRequest(const InferenceRequest &request);

// Decodes one Gemini SSE data chunk (bare GenerateContentResponse — no event:
// types, no [DONE]). candidate parts: text -> TextDelta, thought:true text ->
// ReasoningDelta (with any thoughtSignature as the opaque), functionCall ->
// ToolCallStart + ToolCallArgsDelta (whole args in one shot; id synthesized
// from the name). finishReason -> stop reason (ToolCalls when the chunk also
// carried a functionCall); usageMetadata -> usage. An error object -> Provider.
[[nodiscard]] std::expected<DecodedChunk, AgentError> decodeEvent(const ServerSentEvent &event);

// "STOP"->EndTurn, "MAX_TOKENS"->MaxTokens, "SAFETY"/"RECITATION"/… ->
// ContentFilter, anything else->Other. (ToolCalls is decided by the caller.)
[[nodiscard]] StopReason mapFinishReason(const QString &finishReason);

} // namespace karness::gemini
