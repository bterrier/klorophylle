// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from WatchFlower's src/device_sensor_advertisement.{h,cpp}
// (Copyright (c) 2022 Emeric Grange, GPL-3.0-or-later) — the proven, unit-tested
// pure decoders. Adapted: dropped the `adv_mode` parameter and the DeviceSensor
// glue; the std::optional `Reading` model is produced by the adapter in
// advertisement.{h,cpp}, keeping this file's value type unchanged for fidelity.
#pragma once

#include <cstdint>
#include <vector>

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>

#include "reading.h" // klr_core

namespace klr {

// Sensor values decoded from a single BLE advertisement frame. A field left at
// the sentinel (-99) was absent from the frame or failed range validation.
// (The -99 sentinel is intentionally confined to this ported value type; it is
// converted to std::optional at the adapter boundary — see advertisement.cpp.)
struct SensorAdvData {
    int battery = -99;            // %
    float temperature = -99.f;    // °C
    float humidity = -99.f;       // %
    int soilMoisture = -99;       // %
    int soilConductivity = -99;   // µS/cm
    int luminosity = -99;         // lux
    float hcho = -99.f;           // formaldehyde
    float pressure = -99.f;       // hPa
    float voc = -99.f;            // ppb
    float co2 = -99.f;            // ppm
    float pm25 = -99.f;           // µg/m³
    float pm10 = -99.f;           // µg/m³
};

// Pure decoders for the advertisement formats WatchFlower understood. Each takes
// the 16-bit service-data UUID and the raw service-data bytes, and returns the
// decoded + range-validated values. No I/O, no device state, no signals.
namespace AdvertisementParser {

SensorAdvData decodeXiaomi(uint16_t adv_id, const QByteArray &ba);   // MiBeacon 0xFE95
SensorAdvData decodeQingping(uint16_t adv_id, const QByteArray &ba); // 0xFDCD
SensorAdvData decodeBtHome(uint16_t adv_id, const QByteArray &ba);   // 0xFCD2 (v2), 0x181C/0x181E (v1)

// MiBeacon product id (service-data bytes 2-3, little-endian); 0 if absent.
// Used to tell e.g. Flower Care (0x0098) from RoPot (0x015D).
uint16_t xiaomiProductId(const QByteArray &ba);

// ThermoBeacon manufacturer data (company 0x0010): the 18-byte message carries
// current temperature + humidity (raw / 16); the 20-byte max/min message yields
// nothing. Ported from WatchFlower's device_thermobeacon.cpp.
SensorAdvData decodeThermoBeacon(const QByteArray &mfrData);

// Which measurement objects a decoded frame carried (single bit for MiBeacon).
enum AdvObject : unsigned {
    ADV_BATTERY = (1u << 0),
    ADV_TEMPERATURE = (1u << 1),
    ADV_HUMIDITY = (1u << 2),
    ADV_LUMINOSITY = (1u << 3),
    ADV_MOISTURE = (1u << 4),
    ADV_CONDUCTIVITY = (1u << 5),
    ADV_FORMALDEHYDE = (1u << 6),
    ADV_PRESSURE = (1u << 7),
    ADV_VOC = (1u << 8),
    ADV_CO2 = (1u << 9),
    ADV_PM25 = (1u << 10),
    ADV_PM10 = (1u << 11),
};

unsigned objectMask(const SensorAdvData &d);

} // namespace AdvertisementParser

// Convert a decoded SensorAdvData into Readings — the single place the ported
// -99 sentinel becomes std::optional (absent fields are simply not emitted).
std::vector<Reading> toReadings(const SensorAdvData &d, const QDateTime &at);

// Accumulates advertisement object-type bits into a coherent snapshot across the
// several seconds a sensor takes to rotate through its value types (≈25 s for a
// Flower Care). Pure and deterministic — caller supplies a monotonic nowMs.
// (Not yet wired into the live path; Milestone B's persistence will use it.)
struct AdvSnapshotAccumulator {
    unsigned requiredMask = 0;
    int windowMs = 0;
    unsigned mask = 0;
    int64_t windowStartMs = -1;

    enum Result { Partial, Complete };
    Result feed(unsigned frameMask, int64_t nowMs);
    void reset() { mask = 0; windowStartMs = -1; }
};

} // namespace klr
