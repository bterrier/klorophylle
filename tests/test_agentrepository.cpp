// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "database.h"
#include "inmemoryagentrepository.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqliteagentrepository.h"

using namespace klr;

// Same dual-impl approach as test_journalrepository: every invariant runs against
// both the in-memory fake and the SQLite repo. Agent transcripts are device-local
// (no change_log), so there is no plant/parent to set up — conversations stand alone.
namespace {

AgentConversation makeConversation(const QDateTime &created, const QString &title)
{
    AgentConversation c;
    c.id = ConversationId::generate();
    c.createdAt = created;
    c.title = title;
    return c;
}

AgentMessageRecord makeMessage(ConversationId conv, int seq, int role, const QString &json,
                               const QDateTime &created)
{
    AgentMessageRecord m;
    m.id = AgentMessageId::generate();
    m.conversation = conv;
    m.seq = seq;
    m.role = role;
    m.contentJson = json;
    m.createdAt = created;
    return m;
}

void checkConversationsNewestFirst(IAgentRepository &repo)
{
    const QDateTime t0 = QDateTime::currentDateTimeUtc();
    const AgentConversation older = makeConversation(t0, QStringLiteral("older"));
    const AgentConversation newer = makeConversation(t0.addSecs(3600), QStringLiteral("newer"));
    repo.createConversation(older);
    repo.createConversation(newer);

    const QList<AgentConversation> all = repo.conversations();
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.first().id, newer.id); // newest-first
    QCOMPARE(all.first().title, QStringLiteral("newer"));
    QCOMPARE(all.last().id, older.id);
}

void checkMessagesSeqAscending(IAgentRepository &repo)
{
    const QDateTime t0 = QDateTime::currentDateTimeUtc();
    const AgentConversation conv = makeConversation(t0, QStringLiteral("c"));
    const AgentConversation other = makeConversation(t0, QStringLiteral("other"));
    repo.createConversation(conv);
    repo.createConversation(other);

    // Insert out of seq order; messagesFor must return seq-ascending.
    repo.appendMessage(makeMessage(conv.id, 2, 1, QStringLiteral("{\"two\":true}"), t0.addSecs(2)));
    repo.appendMessage(makeMessage(conv.id, 0, 0, QStringLiteral("{\"zero\":true}"), t0));
    repo.appendMessage(makeMessage(conv.id, 1, 3, QStringLiteral("{\"one\":true}"), t0.addSecs(1)));
    repo.appendMessage(makeMessage(other.id, 0, 0, QStringLiteral("{\"elsewhere\":true}"), t0));

    const QList<AgentMessageRecord> msgs = repo.messagesFor(conv.id);
    QCOMPARE(msgs.size(), 3);                                   // scoped to the conversation
    QCOMPARE(msgs.at(0).seq, 0);
    QCOMPARE(msgs.at(1).seq, 1);
    QCOMPARE(msgs.at(2).seq, 2);
    QCOMPARE(msgs.at(0).role, 0);
    QCOMPARE(msgs.at(2).contentJson, QStringLiteral("{\"two\":true}"));
}

void checkRemoveCascadesMessages(IAgentRepository &repo)
{
    const QDateTime t0 = QDateTime::currentDateTimeUtc();
    const AgentConversation conv = makeConversation(t0, QStringLiteral("c"));
    repo.createConversation(conv);
    repo.appendMessage(makeMessage(conv.id, 0, 0, QStringLiteral("{}"), t0));
    repo.appendMessage(makeMessage(conv.id, 1, 1, QStringLiteral("{}"), t0.addSecs(1)));
    QCOMPARE(repo.messagesFor(conv.id).size(), 2);

    repo.removeConversation(conv.id);
    QVERIFY(repo.conversations().isEmpty());
    QVERIFY(repo.messagesFor(conv.id).isEmpty()); // cascaded
}

} // namespace

class TestAgentRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

private slots:
    void conversationsNewestFirst()
    {
        {
            InMemoryAgentRepository r;
            checkConversationsNewestFirst(r);
        }
        {
            Database db = freshDb();
            SqliteAgentRepository r(db);
            checkConversationsNewestFirst(r);
        }
    }
    void messagesSeqAscending()
    {
        {
            InMemoryAgentRepository r;
            checkMessagesSeqAscending(r);
        }
        {
            Database db = freshDb();
            SqliteAgentRepository r(db);
            checkMessagesSeqAscending(r);
        }
    }
    void removeCascadesMessages()
    {
        {
            InMemoryAgentRepository r;
            checkRemoveCascadesMessages(r);
        }
        {
            Database db = freshDb();
            SqliteAgentRepository r(db);
            checkRemoveCascadesMessages(r);
        }
    }
};

QTEST_GUILESS_MAIN(TestAgentRepository)
#include "test_agentrepository.moc"
