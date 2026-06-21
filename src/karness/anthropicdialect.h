// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dialect.h"

namespace karness {

// The Anthropic Messages dialect (docs/adr/0019 decision 3c). POSTs to
// {baseUrl}/messages with x-api-key + anthropic-version auth, streams typed
// SSE events and completes on `message_stop` (no "[DONE]" sentinel). Pure JSON
// mapping lives in anthropiccodec; this only adapts it to the Dialect seam.
class AnthropicDialect : public Dialect {
public:
    [[nodiscard]] QUrl endpoint(const QUrl &baseUrl, const InferenceRequest &request) const override;
    void applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const override;
    [[nodiscard]] std::expected<QJsonObject, AgentError>
    encodeRequest(const InferenceRequest &request) const override;
    [[nodiscard]] std::expected<DecodedChunk, AgentError>
    decodeEvent(const ServerSentEvent &event) const override;
    [[nodiscard]] bool isTerminalSentinel(const ServerSentEvent &event) const override;
};

} // namespace karness
