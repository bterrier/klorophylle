// SPDX-License-Identifier: GPL-3.0-or-later
#include "geminidialect.h"

#include "geminicodec.h"

#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkRequest>

namespace karness {

QUrl GeminiDialect::endpoint(const QUrl &baseUrl, const InferenceRequest &request) const
{
    QUrl url = baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/models/") + request.model
                + QStringLiteral(":streamGenerateContent"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("alt"), QStringLiteral("sse"));
    url.setQuery(query);
    return url;
}

void GeminiDialect::applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const
{
    if (!apiKey.isEmpty()) // header form keeps the key out of the URL / logs
        netRequest.setRawHeader("x-goog-api-key", apiKey.toUtf8());
}

std::expected<QJsonObject, AgentError>
GeminiDialect::encodeRequest(const InferenceRequest &request) const
{
    return gemini::encodeRequest(request);
}

std::expected<DecodedChunk, AgentError>
GeminiDialect::decodeEvent(const ServerSentEvent &event) const
{
    return gemini::decodeEvent(event);
}

} // namespace karness
