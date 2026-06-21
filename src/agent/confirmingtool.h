// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "itool.h"

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QPromise>

#include <deque>
#include <functional>
#include <memory>

namespace klr {

// A user-confirmation decorator over any karness::ITool (ADR 0019 decision 5/8: the single
// write tool, add_journal_entry, executes only after the user approves). It honors the ITool
// contract with ZERO loop knowledge: invoke() returns a future that stays PENDING
// until approve()/reject() resolves it, so AgentSession simply waits (bounded by its turn
// timeout). spec() passes the inner tool through unchanged, so the model is unaware.
//
// A QObject so the view-model can connect to confirmationRequested and call approve()/reject().
//
// A single turn can carry several parallel calls to the wrapped tool, so invoke() may be
// entered again before the previous confirmation resolves. Requests are therefore queued
// FIFO — each entry owns its own promise/args/summary — and surfaced one at a time: only the
// head fires confirmationRequested, and approve()/reject() resolve the head then advance to
// the next. This way no call is silently dropped (an emplaced single promise would overwrite,
// and the orphaned future would finish empty → a soft error the user never saw).
class ConfirmingTool final : public QObject, public karness::ITool {
    Q_OBJECT

public:
    // summarize turns the call args into a human-readable line for the confirmation prompt
    // (e.g. "Add a Watering entry: gave it a good soak"). Defaults to "<tool name>" when null.
    explicit ConfirmingTool(karness::ITool &inner,
                            std::function<QString(const QJsonObject &)> summarize = {},
                            QObject *parent = nullptr);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

    [[nodiscard]] bool hasPending() const { return !m_queue.empty(); }

    // Resolve the head confirmation, then surface the next queued one (if any): approve() runs
    // the inner tool and forwards its outcome; reject() resolves an isError outcome the model
    // can recover from. No-op when the queue is empty.
    void approve();
    void reject();

    // Abandon every queued confirmation (finishing each promise), e.g. on a cancelled/failed
    // turn — so leftover entries never suppress the next turn's first prompt. The session
    // ignores the abandoned futures via its generation check.
    void reset();

signals:
    void confirmationRequested(const QString &summary, const QJsonObject &args);

private:
    // One pending confirmation: its own promise (resolved on approve/reject), the call args,
    // and the human summary surfaced when it reaches the head of the queue.
    struct Pending {
        QPromise<karness::ToolOutcome> promise;
        QJsonObject args;
        QString summary;
    };

    // Emit confirmationRequested for the current head (no-op when the queue is empty).
    void emitHead();

    karness::ITool &m_inner;
    std::function<QString(const QJsonObject &)> m_summarize;
    std::deque<std::unique_ptr<Pending>> m_queue;
};

} // namespace klr
