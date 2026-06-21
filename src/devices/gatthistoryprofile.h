// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gattprofile.h" // GattDecodeFn (battery), klr_devices
#include "reading.h"      // klr_core

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <functional>
#include <vector>

namespace klr {

// Decode the history characteristics. The count read returns a uint16 record count; device-time a
// uint32 uptime-seconds; an entry the per-record payload, stamped at its absolute time
// (deviceWallEpochMs + its uptime). Pure (the device's static fns) — the same seam the advertisement
// + one-shot GATT decoders use.
using HistoryCountFn = std::function<int(const QByteArray &)>;
using HistoryDeviceTimeFn = std::function<qint64(const QByteArray &)>;
using HistoryEntryFn = std::function<std::vector<Reading>(const QByteArray &, qint64 deviceWallEpochMs)>;

// A Qt-Bluetooth-FREE description of how to download a device's stored history log over GATT
// (ADR 0014). It declares the service/characteristic UUIDs, the control bytes, the pure decoders,
// and the optional MiBeacon RC4 handshake. The stateful connect → (handshake) → mode → device-time
// → count → loop{select, read} → battery → disconnect machine that consumes it lives in
// GattHistorySession (klr_ble); challenge/response can't be declarative, so the handshake is driven
// imperatively there using mibeacon_auth + the fields below. A device returns one from
// Device::gattHistoryProfile() (nullopt = no history path).
struct GattHistoryProfile {
    QString service;                  // history service (Flower Care family: 0x1206)
    QString controlCharacteristic;    // 0x1a10 — write modeCommand, then per-entry addresses
    QString dataCharacteristic;       // 0x1a11 — read the count first, then each entry payload
    QString deviceTimeCharacteristic; // 0x1a12 — uint32 LE uptime seconds
    QByteArray modeCommand;           // written to controlCharacteristic to enter history mode

    // index (0 = newest) -> the bytes to write to controlCharacteristic to select that entry.
    std::function<QByteArray(int index)> addressFor;
    int entriesPerHour = 1;           // the device's log cadence (1 for Flower Care / RoPot)

    HistoryCountFn decodeCount;
    HistoryDeviceTimeFn decodeDeviceTime;
    HistoryEntryFn decodeEntry;

    // Battery is connection-only for these sensors: read it at the end of every
    // session. It lives on a DIFFERENT service from history (Flower Care data service 0x1204).
    QString batteryService;
    QString batteryCharacteristic;    // 0x1a02 (byte 0 = battery %)
    GattDecodeFn decodeBattery;

    // MiBeacon RC4 "verify" handshake — current Flower Care / RoPot firmware gates the history
    // service behind it. The challenge/finish tokens are derived from the device MAC + productId at
    // session time (mibeacon_auth.h).
    bool needsHandshake = false;
    quint16 productId = 0;
    QString handshakeService;              // 0xfe95
    QString handshakeStartCharacteristic;  // 0x0010 — write the fixed start command
    QString handshakeAuthCharacteristic;   // 0x0001 — write the challenge, then the finish token

    int timeoutMs = 30000;            // a whole-buffer download is slower than a one-shot read
};

} // namespace klr
