// SPDX-License-Identifier: GPL-3.0-or-later
#include "liveness.h"

namespace klr {

Liveness livenessOf(std::optional<qint64> lastBroadcastMs, std::optional<qint64> lastValueMs,
                    qint64 nowMs, qint64 offlineMs, qint64 staleValueMs)
{
    // Never heard, or not heard within the offline window — the radio is gone.
    if (!lastBroadcastMs || nowMs - *lastBroadcastMs > offlineMs)
        return Liveness::Offline;
    // Heard recently, but no usable value (or only a stale one) — broadcasting silence.
    if (!lastValueMs || nowMs - *lastValueMs > staleValueMs)
        return Liveness::Stale;
    return Liveness::Live;
}

} // namespace klr
