// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "streamevent.h"

#include <QtCore/QList>
#include <QtCore/QString>

namespace karness {

// Rewrites a compat-dialect TextDelta stream so a LEADING <think>...</think>
// section becomes ReasoningDelta events (docs/adr/0019 decision 6 — local
// models served over Chat Completions emit reasoning inline as <think>
// tags). Pure and incremental: tags split across deltas are handled by
// withholding ambiguous prefixes until they resolve. Only a leading tag
// (after optional whitespace, which is dropped) is honored — Qwen3 /
// R1-distills emit it first; later occurrences pass through as text, so
// legitimate prose mentioning the tag is never mangled. Whitespace directly
// after </think> is dropped too (models pad with "\n\n"); an unterminated
// think section streams as reasoning to the end. Non-text events pass
// through unchanged — while text is still being withheld they force a
// decision first, so emission order is preserved.
class ThinkTagSplitter {
public:
    [[nodiscard]] QList<StreamEvent> feed(const StreamEvent &event);

    // Stream end: residual text that never resolved into a tag is emitted as
    // TextDelta; a withheld partial </think> inside an (unterminated) think
    // section was real reasoning text and is emitted as ReasoningDelta.
    [[nodiscard]] QList<StreamEvent> flush();

private:
    enum class State {
        AtStart,    // deciding whether the stream opens with <think>
        InThink,    // between the tags; scanning for </think>
        AfterThink, // dropping whitespace right after </think>
        PassThrough // tag handled or ruled out; deltas flow untouched
    };

    [[nodiscard]] QList<StreamEvent> processAtStart(const QString &text);
    [[nodiscard]] QList<StreamEvent> processInThink(const QString &text);
    [[nodiscard]] QList<StreamEvent> processAfterThink(const QString &text);

    State m_state = State::AtStart;
    QString m_held; // AtStart: whitespace + partial open tag; InThink: partial close tag
};

} // namespace karness
