// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QDateTime>

#include <optional>

// The repository boundary for per-sensor GATT history-sync bookkeeping (ADR 0014): when this
// install last COMPLETED a history download for a sensor, so the next sweep fetches only newer
// entries and connects at most once per cadence. Device-LOCAL operational state — NOT change-logged
// and never synced across devices (each replica connects on its own schedule). The marker is set to
// `now` on completion, never derived from the last entry (a trimmed read would otherwise leave it in
// the past and re-trigger forever).
//
// The in-memory fake and the SQLite impl pass the SAME behavioural suite, so they can never silently
// diverge.
namespace klr {

class ISyncStateRepository {
public:
    virtual ~ISyncStateRepository() = default;

    // When the last history sync completed for this sensor, or nullopt if never synced.
    virtual std::optional<QDateTime> lastHistorySync(SensorId sensor) const = 0;

    // Record a completed history sync at `when` (upsert).
    virtual void setLastHistorySync(SensorId sensor, const QDateTime &when) = 0;
};

} // namespace klr
