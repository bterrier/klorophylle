// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agenterror.h"
#include "message.h"
#include "streamevent.h"

#include <QtCore/QHash>

#include <expected>

namespace karness {

// Assembles a stream of deltas into the final assistant Message. Pure and
// deterministic; every dialect reuses it instead of hand-rolling assembly.
//
// Semantics:
// - Consecutive deltas of the same kind append to the open block; a kind
//   change closes it and opens a new one (text -> reasoning -> text yields
//   three blocks, order preserved).
// - ToolCallStart opens a pending call keyed by its stream index;
//   ToolCallArgsDelta routes by index (interleaved parallel calls work).
// - Done / ErrorEvent are ignored by feed() — terminal handling is the
//   dialect's job.
// - finish() parses each pending call's accumulated args: empty -> {} (real
//   providers send "" for no-arg tools), malformed -> AgentError::Code::Parse.
//   The assembled message's role is Assistant.
// - A ReasoningDelta may carry providerOpaque (a signature / encrypted item):
//   it is merged onto the open reasoning block (or the most recent one, for a
//   trailing opaque-only frame) so the Done message round-trips the echo blob.
class MessageAccumulator {
public:
    void feed(const StreamEvent &event);
    [[nodiscard]] std::expected<Message, AgentError> finish() const;

private:
    struct PendingCall {
        QString id;
        QString name;
        QString args;
    };

    QList<ContentBlock> m_blocks;        // closed + currently-open delta blocks
    QList<int> m_callOrder;              // stream indices in arrival order
    QHash<int, PendingCall> m_calls;     // keyed by stream index
};

} // namespace karness
