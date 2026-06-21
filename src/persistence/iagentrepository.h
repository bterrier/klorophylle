// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agentconversation.h"
#include "ids.h"

#include <QtCore/QList>

// The repository boundary for AI-agent transcripts (ADR 0019). SqliteAgentRepository
// implements this; tests run the same behavioural suite against InMemoryAgentRepository and
// SqliteAgentRepository (the parity pattern of iplantrepository.h / ijournalrepository.h).
//
// Transcripts are device-local (no change_log) — see agentconversation.h. The store is a thin
// CRUD surface over conversations + their ordered messages; the karness mapping and conversation
// lifecycle live one layer up in klr_agent's Transcript.
namespace klr {

class IAgentRepository {
public:
    virtual ~IAgentRepository() = default;

    virtual void createConversation(const AgentConversation &conversation) = 0;
    virtual QList<AgentConversation> conversations() const = 0;       // newest-first
    virtual void removeConversation(ConversationId conversation) = 0; // cascades its messages

    virtual void appendMessage(const AgentMessageRecord &message) = 0;
    virtual QList<AgentMessageRecord> messagesFor(ConversationId conversation) const = 0; // seq ascending
};

} // namespace klr
