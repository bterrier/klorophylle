// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QByteArrayView>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace karness {

// One framed text/event-stream event. `data` joins multiple data: lines with
// '\n'; `event` is "" when the stream sent no event: field; `id` is the
// last-event-ID seen so far (persists across events, per the WHATWG spec).
struct ServerSentEvent {
    QString event;
    QString data;
    QString id;

    bool operator==(const ServerSentEvent &) const = default;
};

// Incremental, provider-agnostic SSE framing — the one shared parser of
// docs/adr/0019 decision 3 (WHATWG text/event-stream subset: LF and CRLF
// terminators; lone CR unsupported, no real provider emits it). Feed raw
// transport bytes in arbitrary slices and get back every event completed by
// those bytes; partial lines buffer across feeds, so chunk boundaries
// mid-line or mid-UTF-8 are safe. Carries no provider semantics — the
// "[DONE]" sentinel etc. are the dialect's business. A blank line dispatches
// the open event iff it carried at least one data: line (spec rule); comment
// lines (": keepalive") and retry:/unknown fields are skipped; an incomplete
// event at connection end is never dispatched.
class SseParser {
public:
    [[nodiscard]] QList<ServerSentEvent> feed(QByteArrayView bytes);

private:
    void processLine(QByteArrayView line, QList<ServerSentEvent> &out);

    QByteArray m_buffer;     // incomplete trailing line carried across feeds
    QStringList m_dataLines; // data: values of the open event
    QString m_event;
    QString m_id; // last-event-ID; not reset on dispatch (spec)
    bool m_hasData = false;
};

} // namespace karness
