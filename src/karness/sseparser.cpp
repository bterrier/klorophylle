// SPDX-License-Identifier: GPL-3.0-or-later
#include "sseparser.h"

namespace karness {

QList<ServerSentEvent> SseParser::feed(QByteArrayView bytes)
{
    QList<ServerSentEvent> out;
    m_buffer.append(bytes.data(), bytes.size());

    qsizetype lineStart = 0;
    while (true) {
        const qsizetype lf = m_buffer.indexOf('\n', lineStart);
        if (lf < 0)
            break;
        qsizetype lineEnd = lf;
        if (lineEnd > lineStart && m_buffer.at(lineEnd - 1) == '\r')
            --lineEnd; // CRLF
        processLine(QByteArrayView(m_buffer.constData() + lineStart, lineEnd - lineStart), out);
        lineStart = lf + 1;
    }
    m_buffer.remove(0, lineStart);
    return out;
}

void SseParser::processLine(QByteArrayView line, QList<ServerSentEvent> &out)
{
    if (line.isEmpty()) { // blank line: dispatch the open event, if it has data
        if (m_hasData)
            out.append(ServerSentEvent{m_event, m_dataLines.join(u'\n'), m_id});
        m_dataLines.clear();
        m_event.clear();
        m_hasData = false;
        return;
    }
    if (line.front() == ':') // comment / keepalive
        return;

    const qsizetype colon = line.indexOf(':');
    const QByteArrayView field = colon < 0 ? line : line.first(colon);
    QByteArrayView value = colon < 0 ? QByteArrayView() : line.sliced(colon + 1);
    if (!value.isEmpty() && value.front() == ' ')
        value = value.sliced(1); // exactly one leading space is field syntax, not payload

    if (field == "data") {
        m_dataLines.append(QString::fromUtf8(value));
        m_hasData = true;
    } else if (field == "event") {
        m_event = QString::fromUtf8(value);
    } else if (field == "id") {
        const QString id = QString::fromUtf8(value);
        if (!id.contains(QChar(u'\0'))) // spec: NUL-bearing ids are ignored
            m_id = id;
    }
    // retry: and unknown fields are ignored.
}

} // namespace karness
