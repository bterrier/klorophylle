// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iprovider.h"

#include <QtCore/QObject>
#include <QtCore/QPromise>

#include <optional>

// Scripted karness::IProvider for headless agent-loop tests (ADR 0019
// decision 8). Honors the full iprovider.h streaming contract: events are
// pumped ASYNCHRONOUSLY (one per event-loop pass — synchronous emission
// inside generate() would let the consumer re-enter in ways the real
// provider never allows), exactly one terminal Done XOR ErrorEvent, and
// cancel()/destruction terminate an open turn with ErrorEvent{Cancelled}
// + finish(). One ScriptedTurn is consumed per generate(); every
// InferenceRequest is recorded so tests can assert the per-iteration wire
// history.
class FakeProvider : public QObject, public karness::IProvider {
    Q_OBJECT
public:
    struct ScriptedTurn {
        // Should end with the terminal (Done or ErrorEvent) — unless
        // holdOpen, where the turn stays unfinished after the listed
        // events until cancel() or destruction (timeout/cancel tests).
        QList<karness::StreamEvent> events;
        bool holdOpen = false;
    };

    explicit FakeProvider(QObject *parent = nullptr);
    ~FakeProvider() override;

    void setScript(QList<ScriptedTurn> turns);
    void setReady(bool ready) { m_ready = ready; }
    void setCaps(karness::ModelCaps caps) { m_caps = caps; }

    [[nodiscard]] const QList<karness::InferenceRequest> &requests() const { return m_requests; }
    [[nodiscard]] bool turnOpen() const { return m_promise.has_value(); }
    [[nodiscard]] int cancelCount() const { return m_cancelCount; }

    karness::ModelCaps caps() const override { return m_caps; }
    bool isReady() const override { return m_ready; }
    QFuture<karness::StreamEvent> generate(const karness::InferenceRequest &request) override;
    void cancel() override;

private:
    void pumpNext();
    void finishWith(karness::StreamEvent terminal);

    QList<ScriptedTurn> m_script;
    QList<karness::InferenceRequest> m_requests;
    karness::ModelCaps m_caps;
    bool m_ready = true;
    int m_cancelCount = 0;

    std::optional<QPromise<karness::StreamEvent>> m_promise;
    QList<karness::StreamEvent> m_pending;
    bool m_holdOpen = false;
};
