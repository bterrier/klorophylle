// SPDX-License-Identifier: GPL-3.0-or-later
#include "fakeprovider.h"

#include <QtCore/QTimer>

using namespace karness;

FakeProvider::FakeProvider(QObject *parent)
    : QObject(parent)
{
}

FakeProvider::~FakeProvider()
{
    if (m_promise)
        finishWith(ErrorEvent{AgentError{AgentError::Code::Cancelled,
                                         QStringLiteral("provider destroyed mid-turn"), {}}});
}

void FakeProvider::setScript(QList<ScriptedTurn> turns)
{
    m_script = std::move(turns);
}

QFuture<StreamEvent> FakeProvider::generate(const InferenceRequest &request)
{
    m_requests.append(request);

    if (m_promise) { // misuse is loud, same as the real provider
        QPromise<StreamEvent> rejected;
        QFuture<StreamEvent> future = rejected.future();
        rejected.start();
        rejected.addResult(ErrorEvent{AgentError{AgentError::Code::Provider,
                                                 QStringLiteral("a turn is already in flight"), {}}});
        rejected.finish();
        return future;
    }
    if (m_script.isEmpty()) { // test-authoring error, surfaced loudly
        QPromise<StreamEvent> rejected;
        QFuture<StreamEvent> future = rejected.future();
        rejected.start();
        rejected.addResult(ErrorEvent{AgentError{AgentError::Code::Provider,
                                                 QStringLiteral("FakeProvider script exhausted"), {}}});
        rejected.finish();
        return future;
    }

    ScriptedTurn turn = m_script.takeFirst();
    m_pending = std::move(turn.events);
    m_holdOpen = turn.holdOpen;
    m_promise.emplace();
    m_promise->start();
    QFuture<StreamEvent> future = m_promise->future();
    QTimer::singleShot(0, this, &FakeProvider::pumpNext);
    return future;
}

void FakeProvider::cancel()
{
    ++m_cancelCount;
    if (!m_promise)
        return;
    finishWith(ErrorEvent{AgentError{AgentError::Code::Cancelled,
                                     QStringLiteral("turn cancelled"), {}}});
}

void FakeProvider::pumpNext()
{
    if (!m_promise)
        return; // turn already terminated (cancel/destruction)

    if (m_pending.isEmpty()) {
        if (m_holdOpen)
            return; // stall until cancel() or destruction
        // Script ended without a terminal and without holdOpen: an
        // authoring error — fail the test loudly rather than hang.
        finishWith(ErrorEvent{AgentError{AgentError::Code::Parse,
                                         QStringLiteral("FakeProvider script ended without terminal"),
                                         {}}});
        return;
    }

    const StreamEvent event = m_pending.takeFirst();
    if (std::holds_alternative<Done>(event) || std::holds_alternative<ErrorEvent>(event)) {
        finishWith(event);
        return;
    }
    m_promise->addResult(event);
    QTimer::singleShot(0, this, &FakeProvider::pumpNext); // one event per loop pass
}

void FakeProvider::finishWith(StreamEvent terminal)
{
    QPromise<StreamEvent> promise = std::move(*m_promise);
    m_promise.reset(); // single-terminal guard: turn closed before delivery
    m_pending.clear();
    m_holdOpen = false;
    promise.addResult(std::move(terminal));
    promise.finish();
}
