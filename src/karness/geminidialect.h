// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dialect.h"

namespace karness {

// The Gemini generateContent dialect (docs/adr/0019 decision 3d). POSTs to
// {baseUrl}/models/{model}:streamGenerateContent?alt=sse with an x-goog-api-key
// header; streams bare SSE data chunks (no event: types, no "[DONE]") and
// completes on the clean stream end after a finishReason. Pure JSON mapping
// lives in geminicodec; this only adapts it to the Dialect seam.
class GeminiDialect : public Dialect {
public:
    [[nodiscard]] QUrl endpoint(const QUrl &baseUrl, const InferenceRequest &request) const override;
    void applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const override;
    [[nodiscard]] std::expected<QJsonObject, AgentError>
    encodeRequest(const InferenceRequest &request) const override;
    [[nodiscard]] std::expected<DecodedChunk, AgentError>
    decodeEvent(const ServerSentEvent &event) const override;
};

} // namespace karness
