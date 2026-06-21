// SPDX-License-Identifier: GPL-3.0-or-later
#include "confirmingtool.h"

#include <QtCore/QFutureWatcher>

namespace klr {

ConfirmingTool::ConfirmingTool(karness::ITool &inner,
                               std::function<QString(const QJsonObject &)> summarize,
                               QObject *parent)
    : QObject(parent)
    , m_inner(inner)
    , m_summarize(std::move(summarize))
{
}

karness::ToolSpec ConfirmingTool::spec() const
{
    return m_inner.spec(); // pass through — the model is unaware of the confirmation gate
}

QFuture<karness::ToolOutcome> ConfirmingTool::invoke(const QJsonObject &args)
{
    auto pending = std::make_unique<Pending>();
    pending->promise.start();
    pending->args = args;
    pending->summary = m_summarize ? m_summarize(args) : m_inner.spec().name;
    QFuture<karness::ToolOutcome> future = pending->promise.future();

    m_queue.push_back(std::move(pending));
    // Only the head is surfaced; entries queued behind it emit when they reach the head
    // (on the prior one's approve/reject), so the user is asked one at a time.
    if (m_queue.size() == 1)
        emitHead();
    return future;
}

void ConfirmingTool::approve()
{
    if (m_queue.empty())
        return;
    // Detach the head into a shared owner so its (move-only) promise survives inside the
    // copyable watcher lambda, then advance the queue immediately.
    std::shared_ptr<Pending> head = std::move(m_queue.front());
    m_queue.pop_front();

    // Run the inner tool and forward its outcome through this entry's promise. A watcher
    // delivers via the event loop, handling both ready and async inner futures uniformly.
    QFuture<karness::ToolOutcome> inner = m_inner.invoke(head->args);
    auto *watcher = new QFutureWatcher<karness::ToolOutcome>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [watcher, head] {
        const karness::ToolOutcome outcome =
            watcher->future().resultCount() > 0
                ? watcher->future().result()
                : karness::ToolOutcome{ { karness::TextBlock{
                                            QStringLiteral("The action did not complete.") } },
                                        true };
        head->promise.addResult(outcome);
        head->promise.finish();
        watcher->deleteLater();
    });
    watcher->setFuture(inner);

    emitHead(); // surface the next queued confirmation, if any
}

void ConfirmingTool::reject()
{
    if (m_queue.empty())
        return;
    std::unique_ptr<Pending> head = std::move(m_queue.front());
    m_queue.pop_front();
    head->promise.addResult(karness::ToolOutcome{
        { karness::TextBlock{ QStringLiteral("The user declined this action; it was not performed.") } },
        true });
    head->promise.finish();
    emitHead(); // surface the next queued confirmation, if any
}

void ConfirmingTool::reset()
{
    // Finish every queued promise so no QFuture is left dangling; the session ignores these
    // outcomes (the abandoned turn is a stale generation).
    for (std::unique_ptr<Pending> &pending : m_queue) {
        pending->promise.addResult(karness::ToolOutcome{
            { karness::TextBlock{ QStringLiteral("The action was abandoned.") } }, true });
        pending->promise.finish();
    }
    m_queue.clear();
}

void ConfirmingTool::emitHead()
{
    if (m_queue.empty())
        return;
    const Pending &head = *m_queue.front();
    emit confirmationRequested(head.summary, head.args);
}

} // namespace klr
