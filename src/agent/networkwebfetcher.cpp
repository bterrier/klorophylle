// SPDX-License-Identifier: GPL-3.0-or-later
#include "networkwebfetcher.h"

#include "webcontent.h" // isAllowedHost (defense-in-depth, also after redirects)

#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <memory>

namespace klr {

namespace {
// A descriptive User-Agent — Wikimedia's policy 403s requests without one.
const QByteArray kUserAgent = "Klorophylle/1.0 (https://github.com/bterrier/klorophylle)";
constexpr qint64 kMaxBytes = 2 * 1024 * 1024;          // size cap on a fetched page
constexpr std::chrono::milliseconds kTimeout{15000};   // whole-transfer stall guard
} // namespace

NetworkWebFetcher::NetworkWebFetcher(QObject *parent)
    : QObject(parent)
{
}

QFuture<WebFetchResult> NetworkWebFetcher::fetch(const QUrl &url)
{
    auto promise = std::make_shared<QPromise<WebFetchResult>>();
    promise->start();
    QFuture<WebFetchResult> future = promise->future();

    // The caller builds the URL from the allowlist, but re-check here so this seam can never be
    // pointed off-allowlist (decision 5).
    if (!webcontent::isAllowedHost(url)) {
        promise->addResult(WebFetchResult{
            std::nullopt, 0, QStringLiteral("refusing to fetch a host outside the allowlist"), url});
        promise->finish();
        return future;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    request.setRawHeader("Accept", "text/html, text/plain");
    request.setTransferTimeout(kTimeout);

    QNetworkReply *reply = m_network.get(request);

    // Abort early if the body grows past the cap; surfaces as OperationCanceledError below.
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kMaxBytes)
            reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [promise, reply] {
        WebFetchResult result;
        result.finalUrl = reply->url();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (reply->error() == QNetworkReply::OperationCanceledError)
            result.error = QStringLiteral("the page exceeded the size limit");
        else if (reply->error() == QNetworkReply::TimeoutError)
            result.error = QStringLiteral("the request timed out");
        else if (reply->error() != QNetworkReply::NoError)
            result.error = reply->errorString();
        else if (!webcontent::isAllowedHost(result.finalUrl))
            result.error = QStringLiteral("the request redirected off the allowlist");
        else if (result.httpStatus < 200 || result.httpStatus >= 300)
            result.error = QStringLiteral("the server returned HTTP %1").arg(result.httpStatus);
        else if (body.size() > kMaxBytes)
            result.error = QStringLiteral("the page exceeded the size limit");
        else
            result.body = body;

        promise->addResult(result);
        promise->finish();
        reply->deleteLater();
    });

    return future;
}

} // namespace klr
