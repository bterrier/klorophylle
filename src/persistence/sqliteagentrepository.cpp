// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqliteagentrepository.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

namespace {

AgentConversation conversationFromQuery(const QSqlQuery &q)
{
    AgentConversation c;
    c.id = ConversationId{ QUuid::fromString(q.value(QStringLiteral("id")).toString()) };
    c.createdAt = detail::fromIso(q.value(QStringLiteral("created_at")).toString());
    c.title = q.value(QStringLiteral("title")).toString();
    return c;
}

AgentMessageRecord messageFromQuery(const QSqlQuery &q)
{
    AgentMessageRecord m;
    m.id = AgentMessageId{ QUuid::fromString(q.value(QStringLiteral("id")).toString()) };
    m.conversation =
        ConversationId{ QUuid::fromString(q.value(QStringLiteral("conversation_id")).toString()) };
    m.seq = q.value(QStringLiteral("seq")).toInt();
    m.role = q.value(QStringLiteral("role")).toInt();
    m.contentJson = q.value(QStringLiteral("content_json")).toString();
    m.createdAt = detail::fromIso(q.value(QStringLiteral("created_at")).toString());
    return m;
}

} // namespace

void SqliteAgentRepository::createConversation(const AgentConversation &conversation)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(
            QStringLiteral("agent createConversation: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "INSERT INTO agent_conversations(id, created_at, title) "
            "VALUES(:id, :created, COALESCE(:title, ''))"));
        q.bindValue(QStringLiteral(":id"), conversation.id.toString());
        q.bindValue(QStringLiteral(":created"), detail::toIso(conversation.createdAt));
        q.bindValue(QStringLiteral(":title"), conversation.title);
        detail::execPreparedOrThrow(q);
        // Device-local: NOT change-logged (ADR 0014 pattern).
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(
                QStringLiteral("agent createConversation: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

QList<AgentConversation> SqliteAgentRepository::conversations() const
{
    QSqlQuery q(m_db.handle());
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, created_at, title FROM agent_conversations "
        "ORDER BY created_at DESC, id DESC"));
    detail::execPreparedOrThrow(q);
    QList<AgentConversation> out;
    while (q.next())
        out.append(conversationFromQuery(q));
    return out;
}

void SqliteAgentRepository::removeConversation(ConversationId conversation)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(
            QStringLiteral("agent removeConversation: begin failed: %1").arg(d.lastError().text()));
    try {
        // agent_messages cascade-delete via the FK (foreign_keys=ON per connection).
        QSqlQuery q(d);
        detail::prepareOrThrow(q,
            QStringLiteral("DELETE FROM agent_conversations WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), conversation.toString());
        detail::execPreparedOrThrow(q);
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(
                QStringLiteral("agent removeConversation: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqliteAgentRepository::appendMessage(const AgentMessageRecord &message)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(
            QStringLiteral("agent appendMessage: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "INSERT INTO agent_messages(id, conversation_id, seq, role, content_json, created_at) "
            "VALUES(:id, :conv, :seq, :role, :content, :created)"));
        q.bindValue(QStringLiteral(":id"), message.id.toString());
        q.bindValue(QStringLiteral(":conv"), message.conversation.toString());
        q.bindValue(QStringLiteral(":seq"), message.seq);
        q.bindValue(QStringLiteral(":role"), message.role);
        q.bindValue(QStringLiteral(":content"), message.contentJson);
        q.bindValue(QStringLiteral(":created"), detail::toIso(message.createdAt));
        detail::execPreparedOrThrow(q);
        // Device-local: NOT change-logged.
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(
                QStringLiteral("agent appendMessage: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

QList<AgentMessageRecord> SqliteAgentRepository::messagesFor(ConversationId conversation) const
{
    QSqlQuery q(m_db.handle());
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, conversation_id, seq, role, content_json, created_at FROM agent_messages "
        "WHERE conversation_id = :conv ORDER BY seq ASC, id ASC"));
    q.bindValue(QStringLiteral(":conv"), conversation.toString());
    detail::execPreparedOrThrow(q);
    QList<AgentMessageRecord> out;
    while (q.next())
        out.append(messageFromQuery(q));
    return out;
}

} // namespace klr
