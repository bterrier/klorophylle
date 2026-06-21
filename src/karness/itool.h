// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "message.h"
#include "toolspec.h"

#include <QtCore/QFuture>
#include <QtCore/QJsonObject>

namespace karness {

// Outcome of one tool invocation — the body of a ToolResultBlock without the
// callId (the agent loop owns call identity and adds it). isError outcomes are
// re-injected into the conversation so the model can recover; they are not
// turn failures (docs/adr/0019 decisions 5/8).
struct ToolOutcome {
    QList<ContentPart> parts;
    bool isError = false;

    bool operator==(const ToolOutcome &) const = default;
};

// QFuture<T> documents that T needs default + copy constructors (same pin as
// streamevent.h).
static_assert(std::is_default_constructible_v<ToolOutcome>);
static_assert(std::is_copy_constructible_v<ToolOutcome>);

// One callable tool exposed to the agent loop.
//
// Invocation contract (binding for every implementation, mirror of
// iprovider.h):
// - invoke() returns a future that FINISHES on every path; failures are
//   ToolOutcome{isError} values — never QPromise::setException, never a
//   cancelled future. A synchronous tool wraps its result via
//   QtFuture::makeReadyValueFuture.
// - The future may stay unfinished indefinitely (a user-confirmation
//   decorator resolves only when the user decides). AgentSession waits,
//   bounded by its turn timeout and cancel(), which ABANDON the future —
//   a late resolution is ignored, never cancelled from the loop side.
// - args is the parsed ToolCallBlock::args. Schema validation is the tool's
//   job, reported via isError — the harness ships no JSON-Schema validator.
class ITool {
public:
    virtual ~ITool() = default;

    [[nodiscard]] virtual ToolSpec spec() const = 0;
    [[nodiscard]] virtual QFuture<ToolOutcome> invoke(const QJsonObject &args) = 0;
};

} // namespace karness
