// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iagentrepository.h"
#include "message.h"

#include <QtCore/QList>

namespace klr {

class Clock;

// The bridge between karness conversation transcripts and the device-local AgentRepository
// (ADR 0019). This is the ONE place that sees both karness::Message and klr_persistence's
// opaque AgentMessageRecord — the layer boundary kept clean by agentconversation.h: persistence
// stores a role int + a content_json blob, and the karness::messageToJson/messageFromJson codec
// (the canonical, provider-neutral format) does the round-trip here.
namespace transcript {

// Reconstruct a conversation's messages in stored (seq) order. A record whose content_json
// fails to decode (corrupt or forward-incompatible) is SKIPPED, not fatal — one bad row must
// not wipe an otherwise-readable transcript.
[[nodiscard]] QList<karness::Message> load(const IAgentRepository &repo, ConversationId conversation);

// Append messages after the conversation's current tail (seq continues from the max already
// stored), stamping each createdAt from the injected clock. Used to persist the new messages a
// finished turn added to AgentSession::history().
void appendAll(IAgentRepository &repo, ConversationId conversation,
               const QList<karness::Message> &messages, const Clock &clock);

} // namespace transcript

} // namespace klr
