// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iwebfetcher.h"

#include <QtCore/QFuture>

namespace klr {

// In-process web fetcher for tests — returns a scripted WebFetchResult as a ready future and
// records the requested URL + call count, so the web tool (read_online_plant_db) is exercised
// without any network (docs/adr/0023 decision 5).
class FakeWebFetcher final : public IWebFetcher {
public:
    void setResult(WebFetchResult result) { m_result = std::move(result); }

    [[nodiscard]] QUrl lastUrl() const { return m_lastUrl; }
    [[nodiscard]] int callCount() const { return m_calls; }

    QFuture<WebFetchResult> fetch(const QUrl &url) override
    {
        m_lastUrl = url;
        ++m_calls;
        return QtFuture::makeReadyValueFuture(m_result);
    }

private:
    WebFetchResult m_result;
    QUrl m_lastUrl;
    int m_calls = 0;
};

} // namespace klr
