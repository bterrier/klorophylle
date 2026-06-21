// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "thinktagsplitter.h"

using namespace karness;

namespace {

// Feed a sequence of text deltas and collect everything emitted, flush included.
QList<StreamEvent> run(ThinkTagSplitter &splitter, const QStringList &deltas)
{
    QList<StreamEvent> out;
    for (const QString &delta : deltas)
        out.append(splitter.feed(TextDelta{delta}));
    out.append(splitter.flush());
    return out;
}

} // namespace

class TestThinkSplitter : public QObject {
    Q_OBJECT
private slots:
    void noTagPassesThrough()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter, {QStringLiteral("Hello "), QStringLiteral("world")});
        QCOMPARE(out,
                 (QList<StreamEvent>{TextDelta{QStringLiteral("Hello ")},
                                     TextDelta{QStringLiteral("world")}}));
    }

    void leadingTagInOneDelta()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter, {QStringLiteral("<think>dry soil</think>Water it.")});
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("dry soil")},
                                     TextDelta{QStringLiteral("Water it.")}}));
    }

    void openTagSplitAcrossDeltas()
    {
        ThinkTagSplitter splitter;
        const auto out =
            run(splitter, {QStringLiteral("<thi"), QStringLiteral("nk>hmm</think>ok")});
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("hmm")},
                                     TextDelta{QStringLiteral("ok")}}));
    }

    void closeTagSplitAcrossDeltas()
    {
        ThinkTagSplitter splitter;
        const auto out = run(
            splitter,
            {QStringLiteral("<think>hmm"), QStringLiteral("</thi"), QStringLiteral("nk>ok")});
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("hmm")},
                                     TextDelta{QStringLiteral("ok")}}));
    }

    void leadingWhitespaceBeforeTagDropped()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter, {QStringLiteral(" \n<think>a</think>b")});
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("a")},
                                     TextDelta{QStringLiteral("b")}}));
    }

    void whitespaceAfterCloseTagDropped()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter,
                             {QStringLiteral("<think>a</think>"), QStringLiteral("\n"),
                              QStringLiteral("\nAnswer")});
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("a")},
                                     TextDelta{QStringLiteral("Answer")}}));
    }

    void midTextTagPassesThroughAsText()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter, {QStringLiteral("Use the <think>"), QStringLiteral(" tag")});
        QCOMPARE(out,
                 (QList<StreamEvent>{TextDelta{QStringLiteral("Use the <think>")},
                                     TextDelta{QStringLiteral(" tag")}}));
    }

    void secondThinkSectionStaysText()
    {
        ThinkTagSplitter splitter;
        const auto out =
            run(splitter, {QStringLiteral("<think>a</think>b<think>c</think>")});
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("a")},
                                     TextDelta{QStringLiteral("b<think>c</think>")}}));
    }

    void unterminatedThinkFlushesAsReasoning()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter, {QStringLiteral("<think>still pond"), QStringLiteral("ering")});
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("still pond")},
                                     ReasoningDelta{QStringLiteral("ering")}}));
    }

    void partialCloseTagAtStreamEndFlushesAsReasoning()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter, {QStringLiteral("<think>hmm</thi")});
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("hmm")},
                                     ReasoningDelta{QStringLiteral("</thi")}}));
    }

    void partialOpenTagAtStreamEndFlushesAsText()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter, {QStringLiteral("<thi")});
        QCOMPARE(out, (QList<StreamEvent>{TextDelta{QStringLiteral("<thi")}}));
    }

    void whitespaceOnlyStreamFlushesAsText()
    {
        ThinkTagSplitter splitter;
        const auto out = run(splitter, {QStringLiteral("  ")});
        QCOMPARE(out, (QList<StreamEvent>{TextDelta{QStringLiteral("  ")}}));
    }

    void nonTextEventsPassThrough()
    {
        ThinkTagSplitter splitter;
        QList<StreamEvent> out;
        out.append(splitter.feed(TextDelta{QStringLiteral("<think>a</think>b")}));
        out.append(splitter.feed(ToolCallStart{0, QStringLiteral("c1"), QStringLiteral("t")}));
        out.append(splitter.flush());
        QCOMPARE(out,
                 (QList<StreamEvent>{ReasoningDelta{QStringLiteral("a")},
                                     TextDelta{QStringLiteral("b")},
                                     ToolCallStart{0, QStringLiteral("c1"), QStringLiteral("t")}}));
    }

    void nonTextEventForcesHeldTextOut()
    {
        // A structured event while "<thi" is withheld proves it wasn't a tag;
        // order must be preserved: held text first, then the event.
        ThinkTagSplitter splitter;
        QList<StreamEvent> out;
        out.append(splitter.feed(TextDelta{QStringLiteral("<thi")}));
        out.append(splitter.feed(ToolCallStart{0, QStringLiteral("c1"), QStringLiteral("t")}));
        out.append(splitter.flush());
        QCOMPARE(out,
                 (QList<StreamEvent>{TextDelta{QStringLiteral("<thi")},
                                     ToolCallStart{0, QStringLiteral("c1"), QStringLiteral("t")}}));
    }

    void reasoningDeltaPassesThroughUntouched()
    {
        // Structured reasoning (reasoning_content field) coexists with the
        // splitter — it must not be re-interpreted.
        ThinkTagSplitter splitter;
        const auto out = splitter.feed(ReasoningDelta{QStringLiteral("hmm")});
        QCOMPARE(out, (QList<StreamEvent>{ReasoningDelta{QStringLiteral("hmm")}}));
    }
};

QTEST_GUILESS_MAIN(TestThinkSplitter)
#include "test_thinksplitter.moc"
