// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QTimeZone>

#include <cstdint>

namespace klr {

// Xiaomi Flower Care (HHCCJCY01): a plant sensor broadcasting MiBeacon (0xFE95)
// with product id 0x0098. See ../../docs/flowercare-ble-api.md.
//
// Advertisements carry the live values; the device also keeps an on-device history log
// (one entry per hour, ~16 days) read over GATT for backfill (ADR 0014). The history wire
// format is decoded by the pure static fns below (unit-tested in test_historydecoders),
// the same pure-decoder seam the advertisement + one-shot GATT paths use; the stateful
// connect/handshake/read loop that calls them lives in GattHistorySession (klr_ble).
class DeviceFlowerCare final : public Device {
public:
    QString model() const override { return QStringLiteral("Flower Care"); }
    DeviceType type() const override { return DeviceType::PlantSensor; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeXiaomi(0xFE95, adv.serviceData16.value(0xFE95)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return AdvertisementParser::xiaomiProductId(c.adv.serviceData16.value(0xFE95)) == 0x0098;
    }

    // --- History GATT decoders (pure; shared by Flower Care & RoPot) -------------------

    // The history-data characteristic, read first, returns 16 bytes whose [0:2] is a
    // uint16 LE count of stored records.
    static int decodeHistoryCount(const QByteArray &v)
    {
        if (v.size() < 2)
            return 0;
        const auto *d = reinterpret_cast<const quint8 *>(v.constData());
        return d[0] | (d[1] << 8);
    }

    // The device-time characteristic returns a uint32 LE = seconds since the device booted.
    // The wall-clock epoch of boot is then nowMs - deviceTimeSecs*1000, which turns each
    // entry's uptime stamp into an absolute time.
    static qint64 decodeDeviceTimeSecs(const QByteArray &v)
    {
        if (v.size() < 4)
            return 0;
        const auto *d = reinterpret_cast<const quint8 *>(v.constData());
        return qint64(d[0]) | (qint64(d[1]) << 8) | (qint64(d[2]) << 16) | (qint64(d[3]) << 24);
    }

    // One 16-byte history record -> the four sensed quantities, stamped at the entry's
    // absolute time (deviceWallEpochMs + its uptime seconds). Quantities MATCH the
    // advertisement mapping (temperature -> AirTemperature) so history and live form one
    // series. See the byte layout in ../../docs/flowercare-ble-api.md.
    static std::vector<Reading> decodeHistoryEntry(const QByteArray &v, qint64 deviceWallEpochMs)
    {
        if (v.size() < 16)
            return {};
        const auto *d = reinterpret_cast<const quint8 *>(v.constData());
        const quint32 uptime = quint32(d[0]) | (quint32(d[1]) << 8) | (quint32(d[2]) << 16) | (quint32(d[3]) << 24);
        const qint16 tempRaw = qint16(quint16(d[4]) | (quint16(d[5]) << 8));
        const quint32 lux = quint32(d[7]) | (quint32(d[8]) << 8) | (quint32(d[9]) << 16) | (quint32(d[10]) << 24);
        const quint8 moisture = d[11];
        const quint16 cond = quint16(d[12]) | (quint16(d[13]) << 8);
        const QDateTime ts =
            QDateTime::fromMSecsSinceEpoch(deviceWallEpochMs + qint64(uptime) * 1000, QTimeZone::UTC);
        return {
            { Quantity::AirTemperature, tempRaw / 10.0, Unit::DegreeCelsius, ts, Provenance::History },
            { Quantity::Illuminance, double(lux), Unit::Lux, ts, Provenance::History },
            { Quantity::SoilMoisture, double(moisture), Unit::Percent, ts, Provenance::History },
            { Quantity::SoilConductivity, double(cond), Unit::MicroSiemensPerCm, ts, Provenance::History },
        };
    }

    // The battery+firmware characteristic (0x1a02): byte 0 is the battery %. Read on every
    // connection because Flower Care never broadcasts battery in its advertisements
    // (battery is connection-only). Stamped Provenance::Probe (a live connected read, not history).
    static std::vector<Reading> decodeBattery(const QByteArray &v, const QDateTime &at)
    {
        if (v.isEmpty())
            return {};
        const auto *d = reinterpret_cast<const quint8 *>(v.constData());
        return { { Quantity::Battery, double(d[0]), Unit::Percent, at, Provenance::Probe } };
    }

    // The history-download protocol, shared by Flower Care & RoPot (same wire format; only the
    // MiBeacon product id differs). See ../../docs/flowercare-ble-api.md and ADR 0014.
    static GattHistoryProfile historyProfile(quint16 productId)
    {
        GattHistoryProfile p;
        p.service = QStringLiteral("00001206-0000-1000-8000-00805f9b34fb");
        p.controlCharacteristic = QStringLiteral("00001a10-0000-1000-8000-00805f9b34fb");
        p.dataCharacteristic = QStringLiteral("00001a11-0000-1000-8000-00805f9b34fb");
        p.deviceTimeCharacteristic = QStringLiteral("00001a12-0000-1000-8000-00805f9b34fb");
        p.modeCommand = QByteArray::fromHex("a00000"); // enter history mode
        p.addressFor = [](int index) {
            QByteArray a;
            a.append(char(0xa1));
            a.append(char(index & 0xff));
            a.append(char((index >> 8) & 0xff));
            return a;
        };
        p.entriesPerHour = 1;
        p.decodeCount = &DeviceFlowerCare::decodeHistoryCount;
        p.decodeDeviceTime = &DeviceFlowerCare::decodeDeviceTimeSecs;
        p.decodeEntry = &DeviceFlowerCare::decodeHistoryEntry;
        // Battery lives on the data service (0x1204), not the history service.
        p.batteryService = QStringLiteral("00001204-0000-1000-8000-00805f9b34fb");
        p.batteryCharacteristic = QStringLiteral("00001a02-0000-1000-8000-00805f9b34fb");
        p.decodeBattery = &DeviceFlowerCare::decodeBattery;
        // Current firmware gates the history service behind the MiBeacon RC4 handshake.
        p.needsHandshake = true;
        p.productId = productId;
        p.handshakeService = QStringLiteral("0000fe95-0000-1000-8000-00805f9b34fb");
        p.handshakeStartCharacteristic = QStringLiteral("00000010-0000-1000-8000-00805f9b34fb");
        p.handshakeAuthCharacteristic = QStringLiteral("00000001-0000-1000-8000-00805f9b34fb");
        return p;
    }

    std::optional<GattHistoryProfile> gattHistoryProfile() const override
    {
        return historyProfile(0x0098); // HHCCJCY01
    }

    // --- Write commands (ADR 0026) -------------------------------------------------------
    // Ported from WatchFlower's src/devices/device_flowercare.cpp + device_ropot.cpp, cross-checked
    // against docs/flowercare-ble-api.md. Pure byte builders, unit-tested in test_gattcommands.

    static QByteArray ledBlinkPayload() { return QByteArray::fromHex("fdff"); }       // 0x1a00
    static QByteArray clearHistoryPayload() { return QByteArray::fromHex("a20000"); } // 0x1a10

    // The MiBeacon RC4 handshake that gates the history service (same fields as historyProfile).
    static GattHandshake historyHandshake(quint16 productId)
    {
        return { productId,
                 QStringLiteral("0000fe95-0000-1000-8000-00805f9b34fb"),
                 QStringLiteral("00000010-0000-1000-8000-00805f9b34fb"),
                 QStringLiteral("00000001-0000-1000-8000-00805f9b34fb") };
    }

    // The command set shared by Flower Care & RoPot — only the MiBeacon product id differs, and
    // RoPot has no working LED (a TODO stub in WatchFlower), so it passes withLed=false.
    static QList<GattCommand> commandsFor(quint16 productId, bool withLed)
    {
        QList<GattCommand> cmds;
        if (withLed) {
            // LED blink writes to the data service (0x1204) — no handshake needed.
            cmds.push_back({ DeviceAction::LedBlink,
                             QStringLiteral("00001204-0000-1000-8000-00805f9b34fb"),
                             QStringLiteral("00001a00-0000-1000-8000-00805f9b34fb"),
                             ledBlinkPayload(), /*writeWithoutResponse*/ true, std::nullopt });
        }
        // Clear-history writes to the history service (0x1206), which current firmware gates
        // behind the same RC4 handshake as a history read.
        cmds.push_back({ DeviceAction::ClearData,
                         QStringLiteral("00001206-0000-1000-8000-00805f9b34fb"),
                         QStringLiteral("00001a10-0000-1000-8000-00805f9b34fb"),
                         clearHistoryPayload(), /*writeWithoutResponse*/ false,
                         historyHandshake(productId) });
        return cmds;
    }

    QList<GattCommand> gattCommands(const QDateTime &) const override
    {
        return commandsFor(0x0098, /*withLed*/ true);
    }
};

} // namespace klr
