// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/qglobal.h>
#include <optional>

// Pure history-sync scheduling math (ADR 0014). No I/O, no clock reads — the caller passes
// `nowMs`, so it is deterministic under test. A device's on-device log holds the last N hours of
// samples; on a connection we fetch only the entries newer than our last completed sync, gated so a
// connection (which drains the coin-cell) happens at most once per cadence. The reading repository
// upserts per (sensor, quantity, hour) and "NULL never erases" (ADR 0006), so re-reading a few
// already-stored entries is idempotent — hence a small read margin is safe and DB-gap detection is
// unnecessary in v1.
namespace klr {

// How many of the NEWEST history entries to fetch from a sensor's log (entry 0 = newest).
//   entryCount     : the device's reported buffer size (entries currently stored).
//   lastSyncMs     : epoch-ms of our last COMPLETED sync, or nullopt = never synced.
//   nowMs          : current time (epoch ms).
//   entriesPerHour : the device's log cadence (1 for Flower Care / RoPot).
//   marginEntries  : extra entries beyond the computed window, to cover the ±1 index<->hour slop.
// Returns a count clamped to [0, entryCount]. Never synced => entryCount (read all that's stored).
int historyEntriesToRead(int entryCount, std::optional<qint64> lastSyncMs, qint64 nowMs,
                         int entriesPerHour = 1, int marginEntries = 2);

// Whether a sensor is due for a history sync: never synced, or the last sync is at least one
// cadence old. The caller filters its registered, history-capable sensors through this.
bool isHistorySyncDue(std::optional<qint64> lastSyncMs, qint64 nowMs, qint64 cadenceMs);

} // namespace klr
