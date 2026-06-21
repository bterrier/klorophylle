// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reading.h" // klr_core

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>

#include <functional>
#include <optional>
#include <vector>

namespace klr {

// A Qt-Bluetooth-FREE description of how to read a device's current values over a
// GATT connection. UUIDs are plain strings (converted to QBluetoothUuid by the
// GattSession in klr_ble); the per-characteristic decoders are pure functions
// (bytes -> Readings), keeping klr_devices testable and Qt6::Core-only — the same
// pure-decoder seam the advertisement path already uses (see advertisementparser.h).

// Decode one characteristic payload into zero-or-more readings. Pure: no I/O, no
// device state. Stamp readings Provenance::Probe (a one-shot GATT read).
using GattDecodeFn = std::function<std::vector<Reading>(const QByteArray &, const QDateTime &)>;

// One characteristic to read once or subscribe to.
struct GattCharacteristicRead {
    QString characteristic;            // 128-bit or 16-bit UUID string
    enum class Access { Read, Notify } access = Access::Read;
    GattDecodeFn decode;
    QString service;                   // service holding it; empty = the profile's primary service
};

// An optional command to write before reading (e.g. WP6003 "request notify", a
// mode change). makeValue is called at connection time so it can embed a clock.
struct GattTrigger {
    QString characteristic;
    std::function<QByteArray()> makeValue;
    bool writeWithoutResponse = true;
};

// How to read one device. The GattSession connects, runs the triggers, reads /
// subscribes to every `reads` characteristic that exists on the device (absent
// ones are skipped — so a generic profile may list more than a given unit has),
// decodes, then disconnects.
struct GattReadProfile {
    QString service;                   // primary GATT service holding the data
    QList<GattTrigger> triggers;       // optional writes before reading (in order)
    QList<GattCharacteristicRead> reads;
    int timeoutMs = 12000;             // overall connect+read budget
};

// --- Write commands (device actions ported from WatchFlower, ADR 0026) ----------------------
//
// A device "action" is a one-shot GATT WRITE (make the LED blink, water the plant, calibrate,
// clear the on-device log, set the clock) — the only capability that lived exclusively in
// WatchFlower's write paths. A Device returns its supported actions from Device::gattCommands(now); the
// per-action byte payloads come from pure static builders on each device_*.h (the same
// pure-decoder seam used by the advertisement/GATT read paths), so they are unit-testable with
// Qt6::Core only. The stateful connect → (handshake) → write → disconnect machine that consumes
// a GattCommand lives in GattCommandSession (klr_ble); no Qt::Bluetooth here.

enum class DeviceAction { LedBlink, Watering, Calibrate, ClearData, ClockSync };

// MiBeacon RC4 "verify" handshake that gates a write (Flower Care / RoPot clear-history sits
// behind the same handshake as their history service). The challenge/finish tokens are derived
// from the device MAC + productId at session time (mibeacon_auth.h). Same shape as the handshake
// fields on GattHistoryProfile, reused by GattCommandSession.
struct GattHandshake {
    quint16 productId = 0;
    QString service;             // 0xfe95
    QString startCharacteristic; // 0x0010 — write the fixed start command
    QString authCharacteristic;  // 0x0001 — write the challenge, then the finish token
};

// One write command. `payload` is the exact bytes to write to `characteristic` on `service`.
// Time-dependent payloads (ClockSync) are built from the `now` passed to Device::gattCommands,
// so the clock stays injected (no wall-clock read inside the builder). `handshake` runs before
// the write when set.
struct GattCommand {
    DeviceAction action;
    QString service;
    QString characteristic;
    QByteArray payload;
    bool writeWithoutResponse = false;
    std::optional<GattHandshake> handshake;
};

} // namespace klr
