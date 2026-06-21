// SPDX-License-Identifier: GPL-3.0-or-later
#include "responsesdialect.h"

#include "responsescodec.h"

#include <QtNetwork/QNetworkRequest>

namespace karness {

QUrl ResponsesDialect::endpoint(const QUrl &baseUrl, const InferenceRequest &) const
{
    QUrl url = baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/responses"));
    return url;
}

void ResponsesDialect::applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const
{
    if (!apiKey.isEmpty())
        netRequest.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
}

std::expected<QJsonObject, AgentError>
ResponsesDialect::encodeRequest(const InferenceRequest &request) const
{
    return responses::encodeRequest(request);
}

std::expected<DecodedChunk, AgentError>
ResponsesDialect::decodeEvent(const ServerSentEvent &event) const
{
    return responses::decodeEvent(event);
}

} // namespace karness
