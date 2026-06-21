// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dialect.h"

namespace karness {

// The OpenAI Chat Completions dialect (docs/adr/0019 decision 3a) — also the
// compat dialect for Ollama, llama.cpp server, LM Studio, vLLM, OpenRouter and
// any BYOK endpoint. POSTs to {baseUrl}/chat/completions with Bearer auth,
// streams via the "[DONE]" sentinel, and lifts <think> tags from local models.
// Pure JSON mapping lives in chatcompletionscodec; this only adapts it to the
// Dialect seam.
class ChatCompletionsDialect : public Dialect {
public:
    [[nodiscard]] QUrl endpoint(const QUrl &baseUrl, const InferenceRequest &request) const override;
    void applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const override;
    [[nodiscard]] std::expected<QJsonObject, AgentError>
    encodeRequest(const InferenceRequest &request) const override;
    [[nodiscard]] std::expected<DecodedChunk, AgentError>
    decodeEvent(const ServerSentEvent &event) const override;
    [[nodiscard]] bool isTerminalSentinel(const ServerSentEvent &event) const override;
    [[nodiscard]] bool extractsThinkTags() const override { return true; }
};

} // namespace karness
