// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iwebfetcher.h"

#include <QtCore/QObject>
#include <QtNetwork/QNetworkAccessManager>

namespace klr {

// The real IWebFetcher (docs/adr/0023): a GET over QtNetwork with a transfer timeout, a
// response size cap, redirects restricted to no-less-safe, and an allowlist re-check on the final
// URL (a redirect must not escape the allowlist). Lives in klr_agent (QtNetwork comes transitively
// via karness); constructed in the composition root and injected into AgentViewModel (ADR 0002).
class NetworkWebFetcher final : public QObject, public IWebFetcher {
    Q_OBJECT

public:
    explicit NetworkWebFetcher(QObject *parent = nullptr);

    [[nodiscard]] QFuture<WebFetchResult> fetch(const QUrl &url) override;

private:
    QNetworkAccessManager m_network;
};

} // namespace klr
