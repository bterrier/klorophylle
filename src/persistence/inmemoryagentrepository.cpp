// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemoryagentrepository.h"

#include <algorithm>

namespace klr {

void InMemoryAgentRepository::createConversation(const AgentConversation &conversation)
{
    m_conversations.insert(conversation.id.value, conversation);
}

QList<AgentConversation> InMemoryAgentRepository::conversations() const
{
    QList<AgentConversation> out = m_conversations.values();
    // Newest-first; tie-break on id so ordering is deterministic.
    std::sort(out.begin(), out.end(), [](const AgentConversation &a, const AgentConversation &b) {
        if (a.createdAt != b.createdAt)
            return a.createdAt > b.createdAt;
        return a.id.toString() > b.id.toString();
    });
    return out;
}

void InMemoryAgentRepository::removeConversation(ConversationId conversation)
{
    m_conversations.remove(conversation.value);
    m_messages.removeIf([&](const AgentMessageRecord &m) { return m.conversation == conversation; });
}

void InMemoryAgentRepository::appendMessage(const AgentMessageRecord &message)
{
    m_messages.append(message);
}

QList<AgentMessageRecord> InMemoryAgentRepository::messagesFor(ConversationId conversation) const
{
    QList<AgentMessageRecord> out;
    for (const AgentMessageRecord &m : m_messages) {
        if (m.conversation == conversation)
            out.append(m);
    }
    // seq ascending; tie-break on id for determinism.
    std::sort(out.begin(), out.end(), [](const AgentMessageRecord &a, const AgentMessageRecord &b) {
        if (a.seq != b.seq)
            return a.seq < b.seq;
        return a.id.toString() < b.id.toString();
    });
    return out;
}

} // namespace klr
