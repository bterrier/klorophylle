// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"
#include "device_flowercare.h" // shares the history wire format + decoders

namespace klr {

// Xiaomi RoPot (HHCCPOT002): a plant pot sensor broadcasting MiBeacon (0xFE95)
// with product id 0x015D. See ../../docs/ropot-ble-api.md.
class DeviceRoPot final : public Device {
public:
    QString model() const override { return QStringLiteral("RoPot"); }
    DeviceType type() const override { return DeviceType::PlantSensor; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeXiaomi(0xFE95, adv.serviceData16.value(0xFE95)), at);
    }

    // Same on-device history protocol as Flower Care; only the MiBeacon product id differs.
    std::optional<GattHistoryProfile> gattHistoryProfile() const override
    {
        return DeviceFlowerCare::historyProfile(0x015D); // HHCCPOT002
    }

    // Same clear-history command as Flower Care (RoPot has no working LED — a TODO stub in
    // WatchFlower). See ADR 0026.
    QList<GattCommand> gattCommands(const QDateTime &) const override
    {
        return DeviceFlowerCare::commandsFor(0x015D, /*withLed*/ false);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return AdvertisementParser::xiaomiProductId(c.adv.serviceData16.value(0xFE95)) == 0x015D;
    }
};

} // namespace klr
