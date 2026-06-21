// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// SensorBlue ThermoBeacon hygrometers. They broadcast via manufacturer data
// (company id 0x0010) and advertise the name "ThermoBeacon" (LCD + keychain
// variants alike), which is how we recognise them. The 18-byte message carries
// current temperature + humidity. See ../../docs/thermobeacon-ble-api.md.
class DeviceThermoBeacon final : public Device {
public:
    QString model() const override { return QStringLiteral("ThermoBeacon"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeThermoBeacon(adv.manufacturerData.value(0x0010)), at);
    }

    // --- Write commands (ADR 0026) -------------------------------------------------------
    // Ported from WatchFlower's src/devices/device_thermobeacon.cpp, cross-checked against
    // docs/thermobeacon-ble-api.md. Both write a 5-byte command to the TX characteristic (0xFFF5);
    // the device acks by blinking, not on the GATT channel.
    static QByteArray ledBlinkPayload() { return QByteArray::fromHex("0400000000"); }
    static QByteArray clearDataPayload() { return QByteArray::fromHex("0200000000"); }

    QList<GattCommand> gattCommands(const QDateTime &) const override
    {
        const QString service = QStringLiteral("0000ffe0-0000-1000-8000-00805f9b34fb");
        const QString tx = QStringLiteral("0000fff5-0000-1000-8000-00805f9b34fb");
        return {
            { DeviceAction::LedBlink, service, tx, ledBlinkPayload(),
              /*writeWithoutResponse*/ false, std::nullopt },
            { DeviceAction::ClearData, service, tx, clearDataPayload(),
              /*writeWithoutResponse*/ false, std::nullopt },
        };
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("ThermoBeacon"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
