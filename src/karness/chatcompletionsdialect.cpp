// SPDX-License-Identifier: GPL-3.0-or-later
#include "chatcompletionsdialect.h"

#include "chatcompletionscodec.h"

#include <QtNetwork/QNetworkRequest>

namespace karness {

QUrl ChatCompletionsDialect::endpoint(const QUrl &baseUrl, const InferenceRequest &) const
{
    QUrl url = baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/chat/completions"));
    return url;
}

void ChatCompletionsDialect::applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const
{
    if (!apiKey.isEmpty()) // empty -> no header (Ollama, llama-server)
        netRequest.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
}

std::expected<QJsonObject, AgentError>
ChatCompletionsDialect::encodeRequest(const InferenceRequest &request) const
{
    return chatcompletions::encodeRequest(request);
}

std::expected<DecodedChunk, AgentError>
ChatCompletionsDialect::decodeEvent(const ServerSentEvent &event) const
{
    return chatcompletions::decodeChunk(event.data.toUtf8());
}

bool ChatCompletionsDialect::isTerminalSentinel(const ServerSentEvent &event) const
{
    return event.data == QStringLiteral("[DONE]");
}

} // namespace karness
