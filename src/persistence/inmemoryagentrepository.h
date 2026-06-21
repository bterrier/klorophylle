// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iagentrepository.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QUuid>

namespace klr {

// The test/fake agent-transcript repository.
class InMemoryAgentRepository final : public IAgentRepository {
public:
    void createConversation(const AgentConversation &conversation) override;
    QList<AgentConversation> conversations() const override;       // newest-first
    void removeConversation(ConversationId conversation) override; // cascades its messages

    void appendMessage(const AgentMessageRecord &message) override;
    QList<AgentMessageRecord> messagesFor(ConversationId conversation) const override; // seq ascending

private:
    QHash<QUuid, AgentConversation> m_conversations; // keyed by ConversationId::value
    QList<AgentMessageRecord> m_messages;
};

} // namespace klr
