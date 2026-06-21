// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "iagentrepository.h"

// The SQLite-backed agent-transcript repository. The ONLY agent-transcript code that touches SQL.
// Device-local (ADR 0014 pattern): writes are transactional but NOT change-logged.
namespace klr {

class SqliteAgentRepository final : public IAgentRepository {
public:
    explicit SqliteAgentRepository(Database &db) : m_db(db) {}

    void createConversation(const AgentConversation &conversation) override;
    QList<AgentConversation> conversations() const override;       // newest-first
    void removeConversation(ConversationId conversation) override; // cascades its messages

    void appendMessage(const AgentMessageRecord &message) override;
    QList<AgentMessageRecord> messagesFor(ConversationId conversation) const override; // seq ascending

private:
    Database &m_db;
};

} // namespace klr
