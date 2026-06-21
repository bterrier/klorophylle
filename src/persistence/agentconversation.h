// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QDateTime>
#include <QtCore/QString>

// Plain value types for AI-agent transcript persistence (ADR 0019).
//
// IMPORTANT — these stay karness-free on purpose. klr_persistence sits BELOW karness in
// the layer graph (klr_core -> klr_persistence -> klr_agent), so a message is stored as
// OPAQUE primitives: a role int and a content_json string. The karness::Message <-> JSON
// mapping (via karness::messageToJson/messageFromJson) lives in klr_agent's Transcript,
// the one layer allowed to see both. This file knows nothing about content blocks.
//
// Device-LOCAL, like sensor_sync_state (ADR 0014): the agent_* tables are NOT written to
// change_log and must never sync across replicas.
namespace klr {

struct AgentConversation {
    ConversationId id;
    QDateTime createdAt;          // UTC, when the conversation was opened
    QString title;                // freeform label (may be empty)

    bool operator==(const AgentConversation &) const = default;
};

struct AgentMessageRecord {
    AgentMessageId id;
    ConversationId conversation;
    int seq { 0 };                // ordering within the conversation (caller-assigned, 0-based)
    int role { 0 };               // karness::Role as int — opaque projection for queries/inspection
    QString contentJson;          // serialized karness::Message (opaque to this layer)
    QDateTime createdAt;          // UTC

    bool operator==(const AgentMessageRecord &) const = default;
};

} // namespace klr
