// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/qglobal.h>

#include <optional>

// "Is this sensor alive?" — the pure connectivity/liveness judgment. A sensor is
// classified from two timestamps: when it was last *heard* (any advertisement) and
// when it last produced a *usable value*, both relative to now. All pure (no DB/BLE/
// QML, clock-free — nowMs is passed in) and unit-tested with literal inputs, so the
// logic lives here, never in QML/JS. Drives the plant-card dot and
// the per-sensor status surfaces.
namespace klr {

// How alive a sensor looks right now.
enum class Liveness {
    Offline, // not heard within the offline window (or never) -> RED
    Stale,   // heard recently, but no fresh usable value -> ORANGE
    Live,    // heard recently AND a fresh value -> GREEN
};

// Display-only connectivity state, NOT a Liveness: a GATT connection is currently open to the
// device (a one-shot read or a history download). It shares the int space the UI dot reads so a
// single Theme.livenessColor(int) maps everything (Offline/Stale/Live = 0/1/2). Rendered BLUE,
// because during a connection the device stops advertising — every sensor would otherwise show
// Offline, including the very one we are talking to. Surfaces report this in place of Liveness for
// the connected device; it takes precedence over broadcast freshness.
inline constexpr int kConnectivityConnected = 3;

// >this long since the last broadcast -> Offline. 60s: a missed advertisement or two is
// normal, but a minute of silence means the radio is gone / out of range.
inline constexpr qint64 kLivenessOfflineMs = 60'000;
// Newest decoded value older than this (while still broadcasting) -> Stale. Same 60s
// window: the device is heard but its frames carry no fresh usable value.
inline constexpr qint64 kLivenessStaleValueMs = 60'000;

// Classify a sensor's liveness from when it was last heard (any advertisement) and when
// it last produced a usable value, relative to nowMs. nullopt == never observed. The
// comparisons are `now - t > window`, so a future-skewed timestamp reads as fresh (never
// flips a live sensor offline). No sentinels — absence is std::optional.
Liveness livenessOf(std::optional<qint64> lastBroadcastMs,
                    std::optional<qint64> lastValueMs, qint64 nowMs,
                    qint64 offlineMs = kLivenessOfflineMs,
                    qint64 staleValueMs = kLivenessStaleValueMs);

} // namespace klr
