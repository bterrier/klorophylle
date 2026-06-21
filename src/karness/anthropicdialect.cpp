// SPDX-License-Identifier: GPL-3.0-or-later
#include "anthropicdialect.h"

#include "anthropiccodec.h"

#include <QtNetwork/QNetworkRequest>

namespace karness {

QUrl AnthropicDialect::endpoint(const QUrl &baseUrl, const InferenceRequest &) const
{
    QUrl url = baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/messages"));
    return url;
}

void AnthropicDialect::applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const
{
    if (!apiKey.isEmpty())
        netRequest.setRawHeader("x-api-key", apiKey.toUtf8());
    netRequest.setRawHeader("anthropic-version", "2023-06-01");
}

std::expected<QJsonObject, AgentError>
AnthropicDialect::encodeRequest(const InferenceRequest &request) const
{
    return anthropic::encodeRequest(request);
}

std::expected<DecodedChunk, AgentError>
AnthropicDialect::decodeEvent(const ServerSentEvent &event) const
{
    return anthropic::decodeEvent(event);
}

bool AnthropicDialect::isTerminalSentinel(const ServerSentEvent &event) const
{
    return event.event == QStringLiteral("message_stop");
}

} // namespace karness
