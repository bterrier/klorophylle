// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "messageaccumulator.h"

using namespace karness;

class TestKarnessAccumulator : public QObject {
    Q_OBJECT
private slots:
    void textDeltasConcatenate()
    {
        MessageAccumulator acc;
        acc.feed(TextDelta{QStringLiteral("Your basil ")});
        acc.feed(TextDelta{QStringLiteral("needs ")});
        acc.feed(TextDelta{QStringLiteral("water.")});
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QCOMPARE(message->blocks.size(), 1);
        QCOMPARE(std::get<TextBlock>(message->blocks.first()).text,
                 QStringLiteral("Your basil needs water."));
    }

    void kindSwitchOpensNewBlock()
    {
        MessageAccumulator acc;
        acc.feed(TextDelta{QStringLiteral("a")});
        acc.feed(ReasoningDelta{QStringLiteral("hmm ")});
        acc.feed(ReasoningDelta{QStringLiteral("dry soil")});
        acc.feed(TextDelta{QStringLiteral("b")});
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QCOMPARE(message->blocks.size(), 3);
        QCOMPARE(std::get<TextBlock>(message->blocks.at(0)).text, QStringLiteral("a"));
        QCOMPARE(std::get<ReasoningBlock>(message->blocks.at(1)).text,
                 QStringLiteral("hmm dry soil"));
        QCOMPARE(std::get<TextBlock>(message->blocks.at(2)).text, QStringLiteral("b"));
    }

    void toolCallAssembly()
    {
        MessageAccumulator acc;
        acc.feed(ToolCallStart{0, QStringLiteral("c1"), QStringLiteral("read_plant_data")});
        // Args split mid-JSON-token, as real SSE streams do.
        acc.feed(ToolCallArgsDelta{0, QStringLiteral("{\"plant")});
        acc.feed(ToolCallArgsDelta{0, QStringLiteral("Id\": \"p-")});
        acc.feed(ToolCallArgsDelta{0, QStringLiteral("1\"}")});
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QCOMPARE(message->blocks.size(), 1);
        const auto &call = std::get<ToolCallBlock>(message->blocks.first());
        QCOMPARE(call.id, QStringLiteral("c1"));
        QCOMPARE(call.name, QStringLiteral("read_plant_data"));
        QCOMPARE(call.args.value(QStringLiteral("plantId")).toString(), QStringLiteral("p-1"));
    }

    void parallelToolCallsRoutedByIndex()
    {
        MessageAccumulator acc;
        acc.feed(ToolCallStart{0, QStringLiteral("c1"), QStringLiteral("list_plants")});
        acc.feed(ToolCallStart{1, QStringLiteral("c2"), QStringLiteral("read_plant_journal")});
        acc.feed(ToolCallArgsDelta{1, QStringLiteral("{\"plantId\":")});
        acc.feed(ToolCallArgsDelta{0, QStringLiteral("{}")});
        acc.feed(ToolCallArgsDelta{1, QStringLiteral("\"p-2\"}")});
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QCOMPARE(message->blocks.size(), 2);
        const auto &first = std::get<ToolCallBlock>(message->blocks.at(0));
        const auto &second = std::get<ToolCallBlock>(message->blocks.at(1));
        QCOMPARE(first.id, QStringLiteral("c1"));
        QVERIFY(first.args.isEmpty());
        QCOMPARE(second.id, QStringLiteral("c2"));
        QCOMPARE(second.args.value(QStringLiteral("plantId")).toString(), QStringLiteral("p-2"));
    }

    void emptyArgsBecomesEmptyObject()
    {
        MessageAccumulator acc;
        acc.feed(ToolCallStart{0, QStringLiteral("c1"), QStringLiteral("list_plants")});
        // No args delta at all — providers send nothing for no-arg tools.
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QVERIFY(std::get<ToolCallBlock>(message->blocks.first()).args.isEmpty());
    }

    void malformedArgsYieldsParseError()
    {
        MessageAccumulator acc;
        acc.feed(ToolCallStart{0, QStringLiteral("c1"), QStringLiteral("read_plant_data")});
        acc.feed(ToolCallArgsDelta{0, QStringLiteral("{\"plantId\": ")}); // truncated
        const auto message = acc.finish();
        QVERIFY(!message.has_value());
        QCOMPARE(message.error().code, AgentError::Code::Parse);
    }

    void reasoningProviderOpaqueMergedOntoBlock()
    {
        // Native dialects stream reasoning text, then a trailing opaque-only
        // frame (an Anthropic signature / Responses encrypted item) that must
        // land on the same reasoning block so a tool loop echoes it verbatim.
        MessageAccumulator acc;
        acc.feed(ReasoningDelta{QStringLiteral("dry "), std::nullopt});
        acc.feed(ReasoningDelta{QStringLiteral("soil"), std::nullopt});
        acc.feed(ReasoningDelta{QString(),
                                QJsonObject{{QStringLiteral("signature"), QStringLiteral("sig-xyz")}}});
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QCOMPARE(message->blocks.size(), 1);
        const auto &block = std::get<ReasoningBlock>(message->blocks.first());
        QCOMPARE(block.text, QStringLiteral("dry soil"));
        QCOMPARE(block.providerOpaque.value(QStringLiteral("signature")).toString(),
                 QStringLiteral("sig-xyz"));
    }

    void trailingOpaqueAfterReasoningClosedStillMerges()
    {
        // Opaque arriving after a text block opened (reasoning no longer last)
        // attaches to the most recent reasoning block, not a fresh one.
        MessageAccumulator acc;
        acc.feed(ReasoningDelta{QStringLiteral("hmm"), std::nullopt});
        acc.feed(TextDelta{QStringLiteral("answer")});
        acc.feed(ReasoningDelta{QString(),
                                QJsonObject{{QStringLiteral("id"), QStringLiteral("rs_1")}}});
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QCOMPARE(message->blocks.size(), 2); // no new reasoning block appended
        const auto &reasoning = std::get<ReasoningBlock>(message->blocks.at(0));
        QCOMPARE(reasoning.text, QStringLiteral("hmm"));
        QCOMPARE(reasoning.providerOpaque.value(QStringLiteral("id")).toString(),
                 QStringLiteral("rs_1"));
        QCOMPARE(std::get<TextBlock>(message->blocks.at(1)).text, QStringLiteral("answer"));
    }

    void doneAndErrorEventsIgnoredByFeed()
    {
        MessageAccumulator acc;
        acc.feed(TextDelta{QStringLiteral("x")});
        acc.feed(Done{});
        acc.feed(ErrorEvent{AgentError{AgentError::Code::Network, QStringLiteral("boom"), {}}});
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QCOMPARE(message->blocks.size(), 1);
    }

    void finishedMessageRoleIsAssistant()
    {
        MessageAccumulator acc;
        acc.feed(TextDelta{QStringLiteral("x")});
        const auto message = acc.finish();
        QVERIFY(message.has_value());
        QCOMPARE(message->role, Role::Assistant);
    }
};

QTEST_GUILESS_MAIN(TestKarnessAccumulator)
#include "test_karnessaccumulator.moc"
