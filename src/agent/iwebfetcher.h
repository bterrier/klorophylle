// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QFuture>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <optional>

namespace klr {

// The result of one web fetch (docs/adr/0023). `body` is present only on a clean 2xx within
// the size cap; `error` is present on any failure (transport, non-2xx, off-allowlist redirect, too
// large). `finalUrl` is the URL after redirects — the fetcher re-checks it against the allowlist.
struct WebFetchResult {
    std::optional<QByteArray> body;
    int httpStatus = 0;           // 0 == no HTTP response reached (transport error)
    std::optional<QString> error; // human-readable; surfaced to the model as an isError outcome
    QUrl finalUrl;
};

// The seam for fetching a web page (docs/adr/0023 decision 5), mirroring ISecretStore: klr_agent
// owns the interface, NetworkWebFetcher is the real QtNetwork-backed implementation, and a
// FakeWebFetcher stands in for tests so the web tool + agent loop stay offline. Fetch is GET-only
// and host-restricted; the caller (the tool) builds the URL from the allowlist (the model never
// picks URLs).
class IWebFetcher {
public:
    virtual ~IWebFetcher() = default;

    [[nodiscard]] virtual QFuture<WebFetchResult> fetch(const QUrl &url) = 0;
};

} // namespace klr
