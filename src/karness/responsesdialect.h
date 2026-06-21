// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dialect.h"

namespace karness {

// The OpenAI Responses dialect (docs/adr/0019 decision 3b). POSTs to
// {baseUrl}/responses with Bearer auth, streams typed SSE events and completes
// when the stream closes after response.completed (no "[DONE]" sentinel — the
// stop reason and usage ride that event, so it is decoded, not short-circuited).
// Pure JSON mapping lives in responsescodec; this only adapts it to the seam.
class ResponsesDialect : public Dialect {
public:
    [[nodiscard]] QUrl endpoint(const QUrl &baseUrl, const InferenceRequest &request) const override;
    void applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const override;
    [[nodiscard]] std::expected<QJsonObject, AgentError>
    encodeRequest(const InferenceRequest &request) const override;
    [[nodiscard]] std::expected<DecodedChunk, AgentError>
    decodeEvent(const ServerSentEvent &event) const override;
};

} // namespace karness
