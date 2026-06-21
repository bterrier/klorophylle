// SPDX-License-Identifier: GPL-3.0-or-later
#include "historysync.h"

#include "bucket.h" // kHourMs

#include <algorithm>

namespace klr {

int historyEntriesToRead(int entryCount, std::optional<qint64> lastSyncMs, qint64 nowMs,
                         int entriesPerHour, int marginEntries)
{
    if (entryCount <= 0)
        return 0;
    if (entriesPerHour < 1)
        entriesPerHour = 1;
    if (marginEntries < 0)
        marginEntries = 0;

    // Never synced: read everything the device still holds.
    if (!lastSyncMs)
        return entryCount;

    const qint64 elapsedMs = nowMs - *lastSyncMs;
    if (elapsedMs <= 0) // just synced (or clock skew) — only the margin, idempotent
        return std::min(marginEntries, entryCount);

    // Ceil to whole hours so a partial hour still pulls its entry, then add the slop margin.
    const qint64 hours = (elapsedMs + kHourMs - 1) / kHourMs;
    const qint64 need = hours * entriesPerHour + marginEntries;
    return static_cast<int>(std::clamp<qint64>(need, 0, entryCount));
}

bool isHistorySyncDue(std::optional<qint64> lastSyncMs, qint64 nowMs, qint64 cadenceMs)
{
    if (!lastSyncMs)
        return true;
    return (nowMs - *lastSyncMs) >= cadenceMs;
}

} // namespace klr
