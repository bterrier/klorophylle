// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "agentsession.h"
#include "fakeprovider.h"
#include "faketool.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDeadlineTimer>

#include <functional>

using namespace karness;

namespace {

InferenceRequest simpleRequest()
{
    InferenceRequest request;
    request.model = QStringLiteral("test-model");
    request.messages = {Message{Role::User, {TextBlock{QStringLiteral("hi")}}}};
    return request;
}

bool waitFinished(const QFuture<StreamEvent> &future, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!future.isFinished() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return future.isFinished();
}

bool waitForResults(const QFuture<StreamEvent> &future, int count, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (future.resultCount() < count && !future.isFinished() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return future.resultCount() >= count;
}

// The terminal must be the LAST event and unique — assert while extracting.
const ErrorEvent *terminalError(const QList<StreamEvent> &events)
{
    if (events.isEmpty())
        return nullptr;
    for (qsizetype i = 0; i < events.size() - 1; ++i)
        if (std::holds_alternative<ErrorEvent>(events.at(i))
            || std::holds_alternative<Done>(events.at(i)))
            return nullptr; // terminal not last / not unique
    return std::get_if<ErrorEvent>(&events.constLast());
}

const Done *terminalDone(const QList<StreamEvent> &events)
{
    if (events.isEmpty())
        return nullptr;
    for (qsizetype i = 0; i < events.size() - 1; ++i)
        if (std::holds_alternative<ErrorEvent>(events.at(i))
            || std::holds_alternative<Done>(events.at(i)))
            return nullptr;
    return std::get_if<Done>(&events.constLast());
}

Done doneText(const QString &text, StopReason reason = StopReason::EndTurn)
{
    return Done{Message{Role::Assistant, {TextBlock{text}}}, reason, {}};
}

bool waitIdle(const AgentSession &session, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (session.busy() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return !session.busy();
}

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!condition() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return condition();
}

// Pump the event loop dry — for asserting that nothing MORE happens
// (late provider events, abandoned tool outcomes).
void drainEvents(int timeoutMs = 50)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

Done doneCalls(QList<ContentBlock> blocks, StopReason reason = StopReason::ToolCalls)
{
    return Done{Message{Role::Assistant, std::move(blocks)}, reason, {}};
}

// One ordered log across every session signal — emission-order assertions
// fall out of it, and exactly-one-terminal is checked by counting.
struct SessionLog {
    QStringList entries;
    int finished = 0;
    int failed = 0;
    StopReason lastStop = StopReason::EndTurn;
    AgentError lastError;

    explicit SessionLog(AgentSession &session)
    {
        QObject::connect(&session, &AgentSession::busyChanged, [this](bool busy) {
            entries.append(busy ? QStringLiteral("busy:true") : QStringLiteral("busy:false"));
        });
        QObject::connect(&session, &AgentSession::textDelta, [this](const QString &text) {
            entries.append(QStringLiteral("text:") + text);
        });
        QObject::connect(&session, &AgentSession::reasoningDelta, [this](const QString &text) {
            entries.append(QStringLiteral("reasoning:") + text);
        });
        QObject::connect(&session, &AgentSession::toolCallStarted,
                         [this](const QString &callId, const QString &name, const QJsonObject &) {
                             entries.append(QStringLiteral("tool-start:") + name
                                            + QStringLiteral("/") + callId);
                         });
        QObject::connect(&session, &AgentSession::toolCallFinished,
                         [this](const QString &callId, const ToolResultBlock &result) {
                             entries.append(QStringLiteral("tool-done:") + callId
                                            + (result.isError ? QStringLiteral("/error")
                                                              : QStringLiteral("/ok")));
                         });
        QObject::connect(&session, &AgentSession::turnFinished, [this](StopReason reason) {
            ++finished;
            lastStop = reason;
            entries.append(QStringLiteral("finished"));
        });
        QObject::connect(&session, &AgentSession::turnFailed, [this](const AgentError &error) {
            ++failed;
            lastError = error;
            entries.append(QStringLiteral("failed"));
        });
    }
};

} // namespace

class TestAgentSession : public QObject {
    Q_OBJECT
private slots:
    // --- FakeProvider contract smoke (the double itself must honor
    // iprovider.h before anything built on it can be trusted) ---

    void fakePumpsAsynchronouslyInOrder()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("a")}, TextDelta{QStringLiteral("b")},
                          doneText(QStringLiteral("ab"))}}});
        auto future = fake.generate(simpleRequest());
        QCOMPARE(future.resultCount(), 0); // nothing delivered inside generate()
        QVERIFY(waitFinished(future));
        const auto events = future.results();
        QCOMPARE(events.size(), 3);
        QCOMPARE(std::get<TextDelta>(events.at(0)).text, QStringLiteral("a"));
        QCOMPARE(std::get<TextDelta>(events.at(1)).text, QStringLiteral("b"));
        const Done *done = terminalDone(events);
        QVERIFY(done);
        QCOMPARE(done->stopReason, StopReason::EndTurn);
        QVERIFY(!fake.turnOpen());
        QCOMPARE(fake.requests().size(), 1);
        QCOMPARE(fake.requests().first().model, QStringLiteral("test-model"));
    }

    void fakeHoldOpenStaysUnfinishedUntilCancel()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("partial")}}, true}});
        auto future = fake.generate(simpleRequest());
        QVERIFY(waitForResults(future, 1));
        QVERIFY(!future.isFinished());
        QVERIFY(fake.turnOpen());

        fake.cancel();
        QVERIFY(waitFinished(future));
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Cancelled);
        QVERIFY(!fake.turnOpen());
        QCOMPARE(fake.cancelCount(), 1);
    }

    void fakeDestructionTerminatesOpenTurn()
    {
        auto *fake = new FakeProvider;
        fake->setScript({{{TextDelta{QStringLiteral("x")}}, true}});
        auto future = fake->generate(simpleRequest());
        QVERIFY(waitForResults(future, 1));
        delete fake;
        QVERIFY(future.isFinished()); // finished synchronously in the dtor
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Cancelled);
    }

    void fakeSecondGenerateWhileOpenRejected()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("x")}}, true},
                        {{doneText(QStringLiteral("y"))}}});
        auto first = fake.generate(simpleRequest());
        auto second = fake.generate(simpleRequest());
        QVERIFY(waitFinished(second));
        const ErrorEvent *error = terminalError(second.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Provider);
        QVERIFY(fake.turnOpen()); // first turn untouched
        fake.cancel();
        QVERIFY(waitFinished(first));
    }

    void fakeExhaustedScriptRejectsLoudly()
    {
        FakeProvider fake;
        auto future = fake.generate(simpleRequest());
        QVERIFY(waitFinished(future));
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Provider);
    }

    void fakeScriptWithoutTerminalFailsLoudly()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("oops")}}}}); // no Done, no holdOpen
        auto future = fake.generate(simpleRequest());
        QVERIFY(waitFinished(future));
        const ErrorEvent *error = terminalError(future.results());
        QVERIFY(error);
        QCOMPARE(error->error.code, AgentError::Code::Parse);
    }

    // --- AgentSession: single-iteration text turns ---

    void happySingleShotTextTurn()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("Your basil")},
                          TextDelta{QStringLiteral(" needs water.")},
                          doneText(QStringLiteral("Your basil needs water."))}}});
        AgentSession session(fake);
        SessionLog log(session);

        const auto sent = session.send(QStringLiteral("how is my basil?"));
        QVERIFY(sent.has_value());
        QVERIFY(session.busy());
        QVERIFY(waitIdle(session));

        QCOMPARE(log.finished, 1);
        QCOMPARE(log.failed, 0);
        QCOMPARE(log.lastStop, StopReason::EndTurn);
        const QStringList expected{QStringLiteral("busy:true"),
                                   QStringLiteral("text:Your basil"),
                                   QStringLiteral("text: needs water."),
                                   QStringLiteral("busy:false"),
                                   QStringLiteral("finished")};
        QCOMPARE(log.entries, expected);

        QCOMPARE(session.history().size(), 2);
        QCOMPARE(session.history().at(0).role, Role::User);
        QCOMPARE(session.history().at(1).role, Role::Assistant);
        QCOMPARE(std::get<TextBlock>(session.history().at(1).blocks.first()).text,
                 QStringLiteral("Your basil needs water."));
        QCOMPARE(fake.requests().size(), 1);
    }

    void requestShapePerIteration()
    {
        FakeProvider fake;
        fake.setScript({{{doneText(QStringLiteral("ok"))}}});
        AgentSession session(fake, QStringLiteral("You are a plant assistant."));
        ModelConfig cfg;
        cfg.model = QStringLiteral("plant-model");
        cfg.temperature = 0.2;
        cfg.reasoningEffort = ReasoningEffort::Low;
        session.setModelConfig(cfg);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(fake.requests().size(), 1);
        const InferenceRequest &request = fake.requests().first();
        QCOMPARE(request.model, QStringLiteral("plant-model"));
        QCOMPARE(request.temperature, 0.2);
        QCOMPARE(request.reasoningEffort, ReasoningEffort::Low);
        QCOMPARE(request.messages.size(), 2);
        QCOMPARE(request.messages.first().role, Role::System);
        QCOMPARE(std::get<TextBlock>(request.messages.first().blocks.first()).text,
                 QStringLiteral("You are a plant assistant."));
        QCOMPARE(request.messages.last().role, Role::User);

        // The system prompt never enters the transcript.
        QCOMPARE(session.history().size(), 2);
        for (const Message &message : session.history())
            QVERIFY(message.role != Role::System);
    }

    void ambientPrependedToLatestUserNeverStored()
    {
        FakeProvider fake;
        fake.setScript({{{doneText(QStringLiteral("answer one"))}},
                        {{doneText(QStringLiteral("answer two"))}}});
        AgentSession session(fake);
        session.setAmbient(QStringLiteral("CTX"));

        QVERIFY(session.send(QStringLiteral("one")).has_value());
        QVERIFY(waitIdle(session));

        // The current turn's user message carries the ambient as a leading
        // TextBlock; on the wire it joins as "CTX\n\none".
        const InferenceRequest &first = fake.requests().first();
        QCOMPARE(first.messages.last().role, Role::User);
        QCOMPARE(first.messages.last().blocks.size(), 2);
        QCOMPARE(std::get<TextBlock>(first.messages.last().blocks.at(0)).text,
                 QStringLiteral("CTX"));
        QCOMPARE(std::get<TextBlock>(first.messages.last().blocks.at(1)).text,
                 QStringLiteral("one"));

        // Ambient never enters history(): the stored user message stays bare.
        QCOMPARE(session.history().first().role, Role::User);
        QCOMPARE(session.history().first().blocks.size(), 1);
        QCOMPARE(std::get<TextBlock>(session.history().first().blocks.first()).text,
                 QStringLiteral("one"));

        // A second turn: only the new user message carries ambient; the PRIOR
        // user turn is replayed bare, so it stays a stable cacheable prefix.
        QVERIFY(session.send(QStringLiteral("two")).has_value());
        QVERIFY(waitIdle(session));
        const InferenceRequest &second = fake.requests().last();
        QCOMPARE(second.messages.size(), 3); // one, answer one, two
        QCOMPARE(second.messages.at(0).blocks.size(), 1); // prior user: bare
        QCOMPARE(std::get<TextBlock>(second.messages.at(0).blocks.first()).text,
                 QStringLiteral("one"));
        QCOMPARE(second.messages.at(2).blocks.size(), 2); // current user: CTX + two
        QCOMPARE(std::get<TextBlock>(second.messages.at(2).blocks.at(0)).text,
                 QStringLiteral("CTX"));
        QCOMPARE(std::get<TextBlock>(second.messages.at(2).blocks.at(1)).text,
                 QStringLiteral("two"));
    }

    void providerErrorPassthrough_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("httpStatus"); // -1 = none
        QTest::addRow("http401") << static_cast<int>(AgentError::Code::Http)
                                 << QStringLiteral("invalid api key") << 401;
        QTest::addRow("network") << static_cast<int>(AgentError::Code::Network)
                                 << QStringLiteral("connection reset") << -1;
        QTest::addRow("parse") << static_cast<int>(AgentError::Code::Parse)
                               << QStringLiteral("undecodable chunk") << -1;
    }

    void providerErrorPassthrough()
    {
        QFETCH(int, code);
        QFETCH(QString, message);
        QFETCH(int, httpStatus);
        AgentError error{static_cast<AgentError::Code>(code), message, {}};
        if (httpStatus >= 0)
            error.httpStatus = httpStatus;

        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("partial")}, ErrorEvent{error}}}});
        AgentSession session(fake);
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(log.failed, 1);
        QCOMPARE(log.finished, 0);
        QCOMPARE(log.lastError, error); // identical AgentError surfaces
        QVERIFY(session.history().isEmpty()); // rolled back to pre-send
    }

    void notReadyFailsFast()
    {
        FakeProvider fake;
        fake.setReady(false);
        AgentSession session(fake);
        SessionLog log(session);

        const auto sent = session.send(QStringLiteral("hi"));
        QVERIFY(!sent.has_value());
        QCOMPARE(sent.error().code, AgentError::Code::NotReady);
        QVERIFY(!session.busy());
        QVERIFY(fake.requests().isEmpty());
        QVERIFY(log.entries.isEmpty()); // no signals, not even busyChanged
        QVERIFY(session.history().isEmpty());
    }

    void secondSendWhileBusyRejected()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("thinking")},
                          doneText(QStringLiteral("thinking done"))}}});
        AgentSession session(fake);
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("first")).has_value());
        const auto second = session.send(QStringLiteral("second"));
        QVERIFY(!second.has_value());
        QCOMPARE(second.error().code, AgentError::Code::Provider);

        QVERIFY(waitIdle(session)); // first turn unaffected
        QCOMPARE(log.finished, 1);
        QCOMPARE(log.failed, 0);
        QCOMPARE(fake.requests().size(), 1);
        QCOMPARE(session.history().size(), 2);
        QCOMPARE(std::get<TextBlock>(session.history().first().blocks.first()).text,
                 QStringLiteral("first"));
    }

    void emptyDeltasNotForwardedWhitespaceIs()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QString()}, TextDelta{QStringLiteral(" ")},
                          ReasoningDelta{QString()}, doneText(QStringLiteral("ok"))}}});
        AgentSession session(fake);
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        const QStringList expected{QStringLiteral("busy:true"),
                                   QStringLiteral("text: "), // whitespace IS a token
                                   QStringLiteral("busy:false"),
                                   QStringLiteral("finished")};
        QCOMPARE(log.entries, expected);
    }

    void maxTokensFinishesWithPartialKept()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("par")},
                          doneText(QStringLiteral("par"), StopReason::MaxTokens)}}});
        AgentSession session(fake);
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(log.finished, 1);
        QCOMPARE(log.failed, 0);
        QCOMPARE(log.lastStop, StopReason::MaxTokens); // the UI's truncation notice
        QCOMPARE(session.history().size(), 2);         // partial message kept
        QCOMPARE(std::get<TextBlock>(session.history().last().blocks.first()).text,
                 QStringLiteral("par"));
    }

    void contentFilterFinishes()
    {
        FakeProvider fake;
        fake.setScript({{{doneText(QString(), StopReason::ContentFilter)}}});
        AgentSession session(fake);
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(log.finished, 1);
        QCOMPARE(log.lastStop, StopReason::ContentFilter);
        QCOMPARE(session.history().size(), 2);
    }

    void historyAccumulatesAcrossTurns()
    {
        FakeProvider fake;
        fake.setScript({{{doneText(QStringLiteral("answer one"))}},
                        {{doneText(QStringLiteral("answer two"))}}});
        AgentSession session(fake);

        QVERIFY(session.send(QStringLiteral("one")).has_value());
        QVERIFY(waitIdle(session));
        QVERIFY(session.send(QStringLiteral("two")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(session.history().size(), 4);
        QCOMPARE(fake.requests().size(), 2);
        // The second request carries the full prior transcript.
        const InferenceRequest &request = fake.requests().last();
        QCOMPARE(request.messages.size(), 3);
        QCOMPARE(std::get<TextBlock>(request.messages.at(0).blocks.first()).text,
                 QStringLiteral("one"));
        QCOMPARE(std::get<TextBlock>(request.messages.at(1).blocks.first()).text,
                 QStringLiteral("answer one"));
        QCOMPARE(std::get<TextBlock>(request.messages.at(2).blocks.first()).text,
                 QStringLiteral("two"));
    }

    // --- AgentSession: the tool loop ---

    void multiIterationToolLoopHappyPath()
    {
        FakeProvider fake;
        fake.setScript(
            {{{TextDelta{QStringLiteral("checking")},
               doneCalls({TextBlock{QStringLiteral("checking")},
                          ToolCallBlock{QStringLiteral("call_a"), QStringLiteral("list_plants"),
                                        {}}})}},
             {{doneText(QStringLiteral("you have two plants"))}}});
        FakeTool tool(QStringLiteral("list_plants"));
        tool.setOutcome(ToolOutcome{{TextBlock{QStringLiteral("basil, mint")}}, false});
        AgentSession session(fake);
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("what do I grow?")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(log.finished, 1);
        QCOMPARE(log.failed, 0);
        QCOMPARE(tool.invocations().size(), 1);
        QCOMPARE(fake.requests().size(), 2);

        // Tools advertised on every request.
        QCOMPARE(fake.requests().first().tools.size(), 1);
        QCOMPARE(fake.requests().first().tools.first().name, QStringLiteral("list_plants"));

        // Second request carries assistant tool-call message + Role::Tool result.
        const QList<Message> &messages = fake.requests().last().messages;
        QCOMPARE(messages.size(), 3);
        QCOMPARE(messages.at(1).role, Role::Assistant);
        QCOMPARE(messages.at(2).role, Role::Tool);
        const auto &result = std::get<ToolResultBlock>(messages.at(2).blocks.first());
        QCOMPARE(result.callId, QStringLiteral("call_a"));
        QVERIFY(!result.isError);
        QCOMPARE(std::get<TextBlock>(result.parts.first()).text, QStringLiteral("basil, mint"));

        const QStringList expected{QStringLiteral("busy:true"),
                                   QStringLiteral("text:checking"),
                                   QStringLiteral("tool-start:list_plants/call_a"),
                                   QStringLiteral("tool-done:call_a/ok"),
                                   QStringLiteral("busy:false"),
                                   QStringLiteral("finished")};
        QCOMPARE(log.entries, expected);

        // History: [user, assistant(call), tool, assistant].
        QCOMPARE(session.history().size(), 4);
        QCOMPARE(session.history().at(2).role, Role::Tool);
        QCOMPARE(std::get<TextBlock>(session.history().at(3).blocks.first()).text,
                 QStringLiteral("you have two plants"));
    }

    void parallelToolCallsResolvedOutOfOrder()
    {
        QJsonObject argsA{{QStringLiteral("plant"), QStringLiteral("basil")}};
        QJsonObject argsB{{QStringLiteral("plant"), QStringLiteral("mint")}};
        FakeProvider fake;
        fake.setScript(
            {{{doneCalls({ToolCallBlock{QStringLiteral("c1"), QStringLiteral("tool_a"), argsA},
                          ToolCallBlock{QStringLiteral("c2"), QStringLiteral("tool_b"),
                                        argsB}})}},
             {{doneText(QStringLiteral("done"))}}});
        FakeTool toolA(QStringLiteral("tool_a"));
        toolA.setPending(true);
        FakeTool toolB(QStringLiteral("tool_b"));
        toolB.setPending(true);
        AgentSession session(fake);
        session.setTools({&toolA, &toolB});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("check both")).has_value());
        QVERIFY(waitUntil([&] { return toolA.hasPending() && toolB.hasPending(); }));
        QCOMPARE(toolA.invocations().first(), argsA); // each call got its own args
        QCOMPARE(toolB.invocations().first(), argsB);

        // Resolve B BEFORE A: the iteration must wait for both…
        toolB.resolvePending(ToolOutcome{{TextBlock{QStringLiteral("mint ok")}}, false});
        QVERIFY(waitUntil([&] { return !toolB.hasPending(); }));
        QVERIFY(session.busy()); // …still waiting on A
        QCOMPARE(fake.requests().size(), 1);

        toolA.resolvePending(ToolOutcome{{TextBlock{QStringLiteral("basil ok")}}, false});
        QVERIFY(waitIdle(session));
        QCOMPARE(log.finished, 1);

        // …and the single Role::Tool message keeps ORIGINAL CALL order.
        const Message &toolMessage = fake.requests().last().messages.at(2);
        QCOMPARE(toolMessage.role, Role::Tool);
        QCOMPARE(toolMessage.blocks.size(), 2);
        QCOMPARE(std::get<ToolResultBlock>(toolMessage.blocks.at(0)).callId,
                 QStringLiteral("c1"));
        QCOMPARE(std::get<ToolResultBlock>(toolMessage.blocks.at(1)).callId,
                 QStringLiteral("c2"));
    }

    void toolErrorReinjectedAndModelRecovers()
    {
        FakeProvider fake;
        fake.setScript({{{doneCalls({ToolCallBlock{QStringLiteral("c1"),
                                                   QStringLiteral("read_plant_data"), {}}})}},
                        {{doneText(QStringLiteral("sorry, no data available"))}}});
        FakeTool tool(QStringLiteral("read_plant_data"));
        tool.setOutcome(ToolOutcome{{TextBlock{QStringLiteral("no sensor attached")}}, true});
        AgentSession session(fake);
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("soil moisture?")).has_value());
        QVERIFY(waitIdle(session));

        // The error is re-injected, the model recovers — NOT a turn failure.
        QCOMPARE(log.failed, 0);
        QCOMPARE(log.finished, 1);
        QVERIFY(log.entries.contains(QStringLiteral("tool-done:c1/error")));
        const auto &result =
            std::get<ToolResultBlock>(fake.requests().last().messages.at(2).blocks.first());
        QVERIFY(result.isError);
        QCOMPARE(std::get<TextBlock>(result.parts.first()).text,
                 QStringLiteral("no sensor attached"));
        QCOMPARE(session.history().size(), 4); // error result kept in history
    }

    void unknownToolNameReinjected()
    {
        FakeProvider fake;
        fake.setScript({{{doneCalls({ToolCallBlock{QStringLiteral("c1"),
                                                   QStringLiteral("bogus_tool"), {}}})}},
                        {{doneText(QStringLiteral("let me answer without tools"))}}});
        AgentSession session(fake); // no tools registered at all
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(log.failed, 0);
        QCOMPARE(log.finished, 1);
        QVERIFY(log.entries.contains(QStringLiteral("tool-start:bogus_tool/c1")));
        QVERIFY(log.entries.contains(QStringLiteral("tool-done:c1/error")));
        const auto &result =
            std::get<ToolResultBlock>(fake.requests().last().messages.at(2).blocks.first());
        QVERIFY(result.isError);
        QCOMPARE(std::get<TextBlock>(result.parts.first()).text,
                 QStringLiteral("unknown tool: bogus_tool"));
    }

    void loopLimitFails()
    {
        const Done callAgain = doneCalls(
            {ToolCallBlock{QStringLiteral("c"), QStringLiteral("list_plants"), {}}});
        FakeProvider fake;
        fake.setScript({{{callAgain}}, {{callAgain}}, {{doneText(QStringLiteral("retry ok"))}}});
        FakeTool tool(QStringLiteral("list_plants"));
        AgentSession session(fake, {}, AgentSessionConfig{.maxIterations = 2});
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(fake.requests().size(), 2); // a 3rd provider call is never made
        QCOMPARE(log.failed, 1);
        QCOMPARE(log.lastError.code, AgentError::Code::LoopLimit);
        QVERIFY(session.history().isEmpty()); // rolled back
        QVERIFY(!session.busy());

        // A subsequent send works and starts from a clean transcript.
        QVERIFY(session.send(QStringLiteral("again")).has_value());
        QVERIFY(waitIdle(session));
        QCOMPARE(log.finished, 1);
        QCOMPARE(fake.requests().last().messages.size(), 1);
    }

    void toolCallsStopWithoutCallsIsParseFailure()
    {
        FakeProvider fake;
        fake.setScript(
            {{{doneCalls({TextBlock{QStringLiteral("just text")}}, StopReason::ToolCalls)}}});
        AgentSession session(fake);
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(log.failed, 1);
        QCOMPARE(log.lastError.code, AgentError::Code::Parse);
        QVERIFY(session.history().isEmpty());
    }

    void toolCallsRunOnEndTurnStopReason()
    {
        // Compat-server reality: finish_reason "stop" alongside tool calls.
        FakeProvider fake;
        fake.setScript(
            {{{doneCalls({ToolCallBlock{QStringLiteral("c1"), QStringLiteral("list_plants"), {}}},
                         StopReason::EndTurn)}},
             {{doneText(QStringLiteral("two plants"))}}});
        FakeTool tool(QStringLiteral("list_plants"));
        AgentSession session(fake);
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QCOMPARE(tool.invocations().size(), 1); // executed despite EndTurn
        QCOMPARE(fake.requests().size(), 2);
        QCOMPARE(log.finished, 1);
    }

    void maxTokensWithToolCallDoesNotRunTools()
    {
        FakeProvider fake;
        fake.setScript(
            {{{doneCalls({ToolCallBlock{QStringLiteral("c1"), QStringLiteral("list_plants"), {}}},
                         StopReason::MaxTokens)}}});
        FakeTool tool(QStringLiteral("list_plants"));
        AgentSession session(fake);
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));

        QVERIFY(tool.invocations().isEmpty()); // args may be truncated mid-JSON
        QCOMPARE(log.finished, 1);
        QCOMPARE(log.lastStop, StopReason::MaxTokens);
    }

    // --- AgentSession: cancellation ---

    void cancelMidStream()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("partial")}}, true}}); // holdOpen
        AgentSession session(fake);
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitUntil([&] { return log.entries.contains(QStringLiteral("text:partial")); }));

        session.cancel();
        QCOMPARE(log.failed, 1); // terminal emitted synchronously, fail-first
        QCOMPARE(log.lastError.code, AgentError::Code::Cancelled);
        QCOMPARE(fake.cancelCount(), 1);
        QVERIFY(!fake.turnOpen());
        QVERIFY(!session.busy());
        QVERIFY(session.history().isEmpty()); // rolled back

        // The provider's own late Cancelled event must not surface a second
        // terminal.
        const qsizetype entriesAtTerminal = log.entries.size();
        drainEvents();
        QCOMPARE(log.entries.size(), entriesAtTerminal);
        QCOMPARE(log.failed, 1);
    }

    void cancelWhileToolPending()
    {
        FakeProvider fake;
        fake.setScript({{{doneCalls({ToolCallBlock{QStringLiteral("c1"),
                                                   QStringLiteral("slow_tool"), {}}})}}});
        FakeTool tool(QStringLiteral("slow_tool"));
        tool.setPending(true);
        AgentSession session(fake);
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitUntil([&] { return tool.hasPending(); }));

        session.cancel(); // does not wait on the tool
        QCOMPARE(log.failed, 1);
        QCOMPARE(log.lastError.code, AgentError::Code::Cancelled);
        QVERIFY(session.history().isEmpty()); // assistant call message rolled back too
        QVERIFY(!session.busy());

        // A stale resolution is abandoned: no crash, no signal, no history.
        const qsizetype entriesAtTerminal = log.entries.size();
        tool.resolvePending(ToolOutcome{{TextBlock{QStringLiteral("too late")}}, false});
        drainEvents();
        QCOMPARE(log.entries.size(), entriesAtTerminal); // no tool-done after the terminal
        QVERIFY(session.history().isEmpty());
    }

    void cancelWhenIdleIsNoOp()
    {
        FakeProvider fake;
        AgentSession session(fake);
        SessionLog log(session);

        session.cancel();
        drainEvents();
        QVERIFY(log.entries.isEmpty());
        QVERIFY(!session.busy());
        QCOMPARE(fake.cancelCount(), 0); // provider untouched
    }

    void destructionMidTurnIsSilent()
    {
        FakeProvider fake;
        auto *session = new AgentSession(fake);
        SessionLog log(*session);
        fake.setScript({{{TextDelta{QStringLiteral("partial")}}, true}}); // holdOpen

        QVERIFY(session->send(QStringLiteral("hi")).has_value());
        QVERIFY(waitUntil([&] { return log.entries.contains(QStringLiteral("text:partial")); }));

        const qsizetype entriesBeforeDelete = log.entries.size();
        delete session;
        QCOMPARE(log.entries.size(), entriesBeforeDelete); // no signals from the dtor
        QVERIFY(!fake.turnOpen()); // provider's turn closed
        QCOMPARE(fake.cancelCount(), 1);
        drainEvents(); // the provider's late Cancelled event has no receiver left
    }

    // --- AgentSession: per-turn timeout ---

    void turnTimeoutMidStream()
    {
        FakeProvider fake;
        fake.setScript({{{TextDelta{QStringLiteral("partial")}}, true}}); // holdOpen
        AgentSession session(fake, {},
                             AgentSessionConfig{.turnTimeout = std::chrono::milliseconds(50)});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitUntil([&] { return log.failed > 0; }));

        QCOMPARE(log.lastError.code, AgentError::Code::Timeout); // NOT Cancelled
        QCOMPARE(log.finished, 0);
        QCOMPARE(fake.cancelCount(), 1);
        QVERIFY(!fake.turnOpen());
        QVERIFY(!session.busy());
        QVERIFY(session.history().isEmpty()); // rolled back

        const qsizetype entriesAtTerminal = log.entries.size();
        drainEvents(); // the provider's late Cancelled must not resurface
        QCOMPARE(log.entries.size(), entriesAtTerminal);
    }

    void turnTimeoutWhileToolPending()
    {
        FakeProvider fake;
        fake.setScript({{{doneCalls({ToolCallBlock{QStringLiteral("c1"),
                                                   QStringLiteral("slow_tool"), {}}})}}});
        FakeTool tool(QStringLiteral("slow_tool"));
        tool.setPending(true);
        AgentSession session(fake, {},
                             AgentSessionConfig{.turnTimeout = std::chrono::milliseconds(50)});
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitUntil([&] { return tool.hasPending(); }));
        QVERIFY(waitUntil([&] { return log.failed > 0; })); // budget covers tool time

        QCOMPARE(log.lastError.code, AgentError::Code::Timeout);
        QVERIFY(session.history().isEmpty()); // incl. the assistant call message
        QVERIFY(!session.busy());

        // Late resolution after the timeout is abandoned.
        const qsizetype entriesAtTerminal = log.entries.size();
        tool.resolvePending(ToolOutcome{{TextBlock{QStringLiteral("too late")}}, false});
        drainEvents();
        QCOMPARE(log.entries.size(), entriesAtTerminal);
    }

    void turnWithinBudgetIsUnaffected()
    {
        FakeProvider fake;
        fake.setScript({{{doneText(QStringLiteral("quick"))}}});
        AgentSession session(fake, {},
                             AgentSessionConfig{.turnTimeout = std::chrono::seconds(5)});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));
        QCOMPARE(log.finished, 1);
        QCOMPARE(log.failed, 0);

        drainEvents(); // a stopped timer never fires into the next idle period
        QCOMPARE(log.failed, 0);
    }

    // --- AgentSession: integration edges ---

    void reasoningDeltasForwardedAndOpaquePreserved()
    {
        const QJsonObject opaque{{QStringLiteral("signature"), QStringLiteral("sig-abc123")}};
        const Message reasoned{
            Role::Assistant,
            {ReasoningBlock{QStringLiteral("the soil is dry"), opaque},
             ToolCallBlock{QStringLiteral("c1"), QStringLiteral("read_plant_data"), {}}}};
        FakeProvider fake;
        fake.setScript({{{ReasoningDelta{QStringLiteral("the soil")},
                          ReasoningDelta{QStringLiteral(" is dry")},
                          Done{reasoned, StopReason::ToolCalls, {}}}},
                        {{doneText(QStringLiteral("water it"))}}});
        FakeTool tool(QStringLiteral("read_plant_data"));
        AgentSession session(fake);
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));
        QCOMPARE(log.finished, 1);
        QVERIFY(log.entries.contains(QStringLiteral("reasoning:the soil")));
        QVERIFY(log.entries.contains(QStringLiteral("reasoning: is dry")));

        // Done's message enters history VERBATIM, so the signature-bearing
        // opaque blob survives the tool loop and reaches the next request
        // (decision 2's whole point).
        const Message &echoed = fake.requests().last().messages.at(1);
        const auto &reasoning = std::get<ReasoningBlock>(echoed.blocks.first());
        QCOMPARE(reasoning.providerOpaque, opaque);
        QCOMPARE(std::get<ReasoningBlock>(session.history().at(1).blocks.first()).providerOpaque,
                 opaque);
    }

    void signalEmissionOrder()
    {
        FakeProvider fake;
        fake.setScript(
            {{{ReasoningDelta{QStringLiteral("checking")},
               TextDelta{QStringLiteral("let me look")},
               doneCalls({TextBlock{QStringLiteral("let me look")},
                          ToolCallBlock{QStringLiteral("c1"), QStringLiteral("list_plants"),
                                        {}}})}},
             {{TextDelta{QStringLiteral("two plants")},
               doneText(QStringLiteral("two plants"))}}});
        FakeTool tool(QStringLiteral("list_plants"));
        AgentSession session(fake);
        session.setTools({&tool});
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));
        drainEvents(); // nothing may trail the terminal

        const QStringList expected{QStringLiteral("busy:true"),
                                   QStringLiteral("reasoning:checking"),
                                   QStringLiteral("text:let me look"),
                                   QStringLiteral("tool-start:list_plants/c1"),
                                   QStringLiteral("tool-done:c1/ok"),
                                   QStringLiteral("text:two plants"),
                                   QStringLiteral("busy:false"),
                                   QStringLiteral("finished")};
        QCOMPARE(log.entries, expected); // exactly one terminal, strictly last
    }

    void historyCleanAfterFailureThenRetry()
    {
        FakeProvider fake;
        fake.setScript({{{ErrorEvent{AgentError{AgentError::Code::Network,
                                                QStringLiteral("connection reset"), {}}}}},
                        {{doneText(QStringLiteral("recovered"))}}});
        AgentSession session(fake);
        SessionLog log(session);

        QVERIFY(session.send(QStringLiteral("hi")).has_value());
        QVERIFY(waitIdle(session));
        QCOMPARE(log.failed, 1);
        QVERIFY(session.history().isEmpty());

        QVERIFY(session.send(QStringLiteral("hi again")).has_value());
        QVERIFY(waitIdle(session));
        QCOMPARE(log.finished, 1);
        // The retry's request carries no trace of the failed turn.
        QCOMPARE(fake.requests().last().messages.size(), 1);
        QCOMPARE(session.history().size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestAgentSession)
#include "test_agentsession.moc"
