// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "inmemoryagentrepository.h"
#include "message.h"
#include "transcript.h"

using namespace klr;
using namespace karness;

// klr_agent's Transcript maps karness::Message <-> the opaque AgentMessageRecord via the
// canonical message codec. These prove a lossless round-trip across every block variant,
// deterministic seq ordering, seq continuation across appends, and corrupt-row tolerance.
class TestTranscript : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    static Message userText(const QString &t)
    {
        return Message{ Role::User, { TextBlock{ t } } };
    }

private slots:
    void roundTripsAllBlockVariants()
    {
        InMemoryAgentRepository repo;
        const ConversationId conv = ConversationId::generate();
        repo.createConversation(AgentConversation{ conv, QDateTime::currentDateTimeUtc(), {} });

        const QList<Message> sent{
            userText(QStringLiteral("how is my basil?")),
            Message{ Role::Assistant,
                     { ReasoningBlock{ QStringLiteral("let me check"),
                                       QJsonObject{ { QStringLiteral("sig"), QStringLiteral("xyz") } } },
                       TextBlock{ QStringLiteral("checking") },
                       ToolCallBlock{ QStringLiteral("call_1"), QStringLiteral("read_plant_data"),
                                      QJsonObject{ { QStringLiteral("plant_id"), QStringLiteral("p1") } } } } },
            Message{ Role::Tool,
                     { ToolResultBlock{ QStringLiteral("call_1"),
                                        { TextBlock{ QStringLiteral("moisture 12%") },
                                          ImageBlock{ QByteArray("\x01\x02\x03", 3),
                                                      QStringLiteral("image/png") } },
                                        false } } },
            Message{ Role::Assistant, { TextBlock{ QStringLiteral("it needs water") } } },
        };

        transcript::appendAll(repo, conv, sent, m_clock);

        const QList<Message> loaded = transcript::load(repo, conv);
        QCOMPARE(loaded, sent); // lossless, in order
    }

    void seqContinuesAcrossAppends()
    {
        InMemoryAgentRepository repo;
        const ConversationId conv = ConversationId::generate();
        repo.createConversation(AgentConversation{ conv, QDateTime::currentDateTimeUtc(), {} });

        transcript::appendAll(repo, conv, { userText(QStringLiteral("a")) }, m_clock);
        transcript::appendAll(repo, conv,
                              { Message{ Role::Assistant, { TextBlock{ QStringLiteral("b") } } },
                                userText(QStringLiteral("c")) },
                              m_clock);

        const QList<AgentMessageRecord> recs = repo.messagesFor(conv);
        QCOMPARE(recs.size(), 3);
        QCOMPARE(recs.at(0).seq, 0);
        QCOMPARE(recs.at(1).seq, 1);
        QCOMPARE(recs.at(2).seq, 2);

        const QList<Message> loaded = transcript::load(repo, conv);
        QCOMPARE(loaded.size(), 3);
        QCOMPARE(std::get<TextBlock>(loaded.at(2).blocks.first()).text, QStringLiteral("c"));
    }

    void corruptRowIsSkippedNotFatal()
    {
        InMemoryAgentRepository repo;
        const ConversationId conv = ConversationId::generate();
        repo.createConversation(AgentConversation{ conv, QDateTime::currentDateTimeUtc(), {} });

        transcript::appendAll(repo, conv, { userText(QStringLiteral("good")) }, m_clock);
        // A hand-corrupted row at a later seq.
        repo.appendMessage(AgentMessageRecord{ AgentMessageId::generate(), conv, 1, 1,
                                               QStringLiteral("not json"),
                                               QDateTime::currentDateTimeUtc() });

        const QList<Message> loaded = transcript::load(repo, conv);
        QCOMPARE(loaded.size(), 1); // the good row survives
        QCOMPARE(std::get<TextBlock>(loaded.first().blocks.first()).text, QStringLiteral("good"));
    }
};

QTEST_GUILESS_MAIN(TestTranscript)
#include "test_transcript.moc"
