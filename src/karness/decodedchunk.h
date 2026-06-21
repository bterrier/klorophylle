// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "streamevent.h"

#include <QtCore/QList>

#include <optional>

namespace karness {

// Everything one decoded wire event (an SSE frame) can carry: the stream
// events to forward, plus the stopReason/usage a dialect latches for the Done
// the transport assembles at stream end (providers send these on a final
// frame whose payload carries no content). Every Dialect::decodeEvent returns
// this shared shape so StreamingProvider stays dialect-agnostic.
struct DecodedChunk {
    QList<StreamEvent> events;
    std::optional<StopReason> stopReason;
    std::optional<TokenUsage> usage;

    bool operator==(const DecodedChunk &) const = default;
};

} // namespace karness
