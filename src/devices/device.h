// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementdata.h"
#include "gatthistoryprofile.h"
#include "gattprofile.h"
#include "reading.h" // klr_core

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <optional>
#include <vector>

namespace klr {

enum class DeviceType { Generic, PlantSensor, Thermometer, AirQuality };

// What we know about a device at discovery time — used by the registry to pick
// the matching Device subclass.
struct AdvertisementContext {
    QString name;          // advertised name (may be empty)
    AdvertisementData adv; // service + manufacturer data
};

// Abstract device interface. There is ONE concrete subclass per supported device,
// each in its own file (device_*.h). The DeviceRegistry instantiates the matching
// subclass for a discovered advertisement; you then interact with that instance
// (decode advertisements now; GATT/history later). Adding a device = one new file
// + one registration line — never editing a central switch.
class Device {
public:
    virtual ~Device() = default;

    virtual QString model() const = 0;  // human label, e.g. "Flower Care"
    virtual DeviceType type() const = 0;

    // Decode one advertisement frame from this device into readings.
    virtual std::vector<Reading> parseAdvertisement(const AdvertisementData &adv,
                                                    const QDateTime &at) const = 0;

    // How to read this device's current values over a GATT connection, or nullopt
    // if it has no connection path (advertisement-only). Devices that don't
    // broadcast their values override this; the rest inherit the default.
    virtual std::optional<GattReadProfile> gattProfile() const { return std::nullopt; }

    // How to download this device's stored history log over GATT (backfill the hours the
    // app was closed), or nullopt if it has no history path. Overridden by sensors with an
    // on-device log (Flower Care / RoPot); the rest inherit the default. See ADR 0014.
    virtual std::optional<GattHistoryProfile> gattHistoryProfile() const { return std::nullopt; }

    // The one-shot write actions this device supports (LED blink, watering, calibrate, clear the
    // on-device log, clock-sync) — empty for devices with no write actions. `now` builds the
    // time-dependent payloads (ClockSync) with an injected clock; the presence of a command of a
    // given DeviceAction IS the capability (no separate flag). Executed by GattCommandSession
    // (klr_ble). See ADR 0026.
    virtual QList<GattCommand> gattCommands(const QDateTime &now) const
    {
        Q_UNUSED(now);
        return {};
    }
};

} // namespace klr
