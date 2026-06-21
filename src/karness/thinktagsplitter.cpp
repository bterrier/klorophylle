// SPDX-License-Identifier: GPL-3.0-or-later
#include "thinktagsplitter.h"

#include <QtCore/QStringView>

#include <algorithm>
#include <variant>

namespace karness {

namespace {

const QString kOpenTag = QStringLiteral("<think>");
const QString kCloseTag = QStringLiteral("</think>");

// Longest suffix of `text` that is a proper prefix of `tag` — the part that
// must be withheld because the next delta could complete the tag.
qsizetype ambiguousSuffixLength(QStringView text, const QString &tag)
{
    const qsizetype max = std::min(text.size(), tag.size() - 1);
    for (qsizetype length = max; length > 0; --length)
        if (text.endsWith(QStringView(tag).first(length)))
            return length;
    return 0;
}

qsizetype leadingWhitespace(QStringView text)
{
    qsizetype count = 0;
    while (count < text.size() && text.at(count).isSpace())
        ++count;
    return count;
}

} // namespace

QList<StreamEvent> ThinkTagSplitter::feed(const StreamEvent &event)
{
    const auto *delta = std::get_if<TextDelta>(&event);
    if (!delta) {
        // A structured event while text is withheld forces the decision: the
        // held text was not a think tag. Emit it first to preserve order.
        QList<StreamEvent> out;
        if (m_state == State::AtStart && !m_held.isEmpty()) {
            out.append(TextDelta{m_held});
            m_held.clear();
            m_state = State::PassThrough;
        }
        out.append(event);
        return out;
    }

    switch (m_state) {
    case State::AtStart:
        return processAtStart(delta->text);
    case State::InThink:
        return processInThink(delta->text);
    case State::AfterThink:
        return processAfterThink(delta->text);
    case State::PassThrough:
        return {event};
    }
    Q_UNREACHABLE_RETURN({});
}

QList<StreamEvent> ThinkTagSplitter::flush()
{
    QList<StreamEvent> out;
    if (!m_held.isEmpty()) {
        if (m_state == State::AtStart)
            out.append(TextDelta{m_held}); // never resolved into a tag
        else if (m_state == State::InThink)
            out.append(ReasoningDelta{m_held}); // partial close tag was reasoning text
        m_held.clear();
    }
    m_state = State::PassThrough;
    return out;
}

QList<StreamEvent> ThinkTagSplitter::processAtStart(const QString &text)
{
    m_held += text;
    const QStringView held(m_held);
    const QStringView candidate = held.sliced(leadingWhitespace(held));

    if (candidate.isEmpty()) // nothing but whitespace yet — keep deciding
        return {};
    if (candidate.size() < kOpenTag.size()) {
        if (kOpenTag.startsWith(candidate))
            return {}; // could still become <think>
    } else if (candidate.startsWith(kOpenTag)) {
        const QString remainder = candidate.sliced(kOpenTag.size()).toString();
        m_held.clear();
        m_state = State::InThink; // leading whitespace + tag dropped
        return processInThink(remainder);
    }

    // Ruled out: the stream does not open with <think>.
    QList<StreamEvent> out{TextDelta{m_held}};
    m_held.clear();
    m_state = State::PassThrough;
    return out;
}

QList<StreamEvent> ThinkTagSplitter::processInThink(const QString &text)
{
    QList<StreamEvent> out;
    const QString buffer = m_held + text;
    m_held.clear();

    if (const qsizetype close = buffer.indexOf(kCloseTag); close >= 0) {
        if (close > 0)
            out.append(ReasoningDelta{buffer.first(close)});
        m_state = State::AfterThink;
        out.append(processAfterThink(buffer.sliced(close + kCloseTag.size())));
        return out;
    }

    const qsizetype withheld = ambiguousSuffixLength(buffer, kCloseTag);
    if (buffer.size() > withheld)
        out.append(ReasoningDelta{buffer.first(buffer.size() - withheld)});
    m_held = buffer.sliced(buffer.size() - withheld);
    return out;
}

QList<StreamEvent> ThinkTagSplitter::processAfterThink(const QString &text)
{
    const qsizetype skip = leadingWhitespace(text);
    if (skip == text.size()) // still in the post-tag padding
        return {};
    m_state = State::PassThrough;
    return {TextDelta{text.sliced(skip)}};
}

} // namespace karness
