// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "itool.h"

#include <QtCore/QPromise>

#include <optional>
#include <utility>

// Scripted karness::ITool. Ready-outcome mode by default (the future is
// finished on invoke, delivered via the event loop by the session's
// watcher); setPending(true) holds the future open until resolvePending()
// — the shape of the user-confirmation decorator, and what timeout/cancel
// tests need. Honors the itool.h contract: outcomes as values, the future
// finishes on every path (an unresolved pending promise finishes cancelled
// on destruction, which the session surfaces as a soft isError).
class FakeTool : public karness::ITool {
public:
    explicit FakeTool(QString name)
        : m_name(std::move(name))
    {
    }

    void setOutcome(karness::ToolOutcome outcome) { m_outcome = std::move(outcome); }
    void setPending(bool pending) { m_pending = pending; }
    [[nodiscard]] bool hasPending() const { return m_promise.has_value(); }
    void resolvePending(const karness::ToolOutcome &outcome)
    {
        if (!m_promise)
            return;
        m_promise->addResult(outcome);
        m_promise->finish();
        m_promise.reset();
    }

    [[nodiscard]] const QList<QJsonObject> &invocations() const { return m_invocations; }

    karness::ToolSpec spec() const override
    {
        return karness::ToolSpec{m_name, QStringLiteral("fake tool ") + m_name, {}};
    }

    QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override
    {
        m_invocations.append(args);
        if (m_pending) {
            m_promise.emplace();
            m_promise->start();
            return m_promise->future();
        }
        return QtFuture::makeReadyValueFuture(m_outcome);
    }

private:
    QString m_name;
    karness::ToolOutcome m_outcome{{karness::TextBlock{QStringLiteral("ok")}}, false};
    bool m_pending = false;
    std::optional<QPromise<karness::ToolOutcome>> m_promise;
    QList<QJsonObject> m_invocations;
};
