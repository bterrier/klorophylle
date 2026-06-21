// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QDateTime>
#include <optional>

namespace klr {

// What a sensor measures. A sensor *has* a set of these (composition), never the
// ~130 flat fields of WatchFlower's combined Reading type.
enum class Quantity {
    SoilMoisture,
    SoilConductivity,
    SoilTemperature,
    AirTemperature,
    AirHumidity,
    Pressure,
    Illuminance,
    Ppfd,
    Battery,
    Co2,
    Voc,
    Pm25,
    Pm10,
    Hcho,
    Radioactivity,
    WaterTank,
    // Daily Light Integral — a DERIVED quantity: never decoded, stored, or broadcast.
    // It exists only as a CareRange/threshold key (the catalog's mmol·m⁻²·day⁻¹ column) and
    // as a synthesized care verdict computed from the day's illuminance. Appended last (so
    // every stored ordinal of the measured quantities above stays stable); kQuantityCount
    // derives from it.
    Dli, // keep last — kQuantityCount derives from it
};

// Count of Quantity values, for iterating the enum (e.g. resolving one current value
// per quantity). Tied to the last enumerator above.
inline constexpr int kQuantityCount = static_cast<int>(Quantity::Dli) + 1;

enum class Unit {
    None,
    Percent,
    DegreeCelsius,
    MicroSiemensPerCm,
    Lux,
    Micromole,
    Pascal,
    Hectopascal,
    Ppm,
    MicrogramPerCubicMetre,
    MicrosievertPerHour,
    // Display-only alternates (unit preferences; storage stays canonical above).
    DegreeFahrenheit,    // temperature, from DegreeCelsius
    InchOfMercury,       // pressure, from Hectopascal
    MillimetreOfMercury, // pressure, from Hectopascal
};

// How a value reached us — so the UI/agent can tell a live GATT read from a
// passively-cached probe value.
enum class Provenance { Unknown, Live, Advertisement, History, Probe, Manual };

// The canonical storage unit for a quantity. The readings table stores no unit — it
// is a property of the quantity (a SoilMoisture sample is always a Percent) — so this
// is the single source of truth used when reading rows back and by the UI for display
// conversion later. Decoders are expected to produce these units.
constexpr Unit canonicalUnit(Quantity q)
{
    switch (q) {
    case Quantity::SoilMoisture:     return Unit::Percent;
    case Quantity::SoilConductivity: return Unit::MicroSiemensPerCm;
    case Quantity::SoilTemperature:  return Unit::DegreeCelsius;
    case Quantity::AirTemperature:   return Unit::DegreeCelsius;
    case Quantity::AirHumidity:      return Unit::Percent;
    case Quantity::Pressure:         return Unit::Hectopascal;
    case Quantity::Illuminance:      return Unit::Lux;
    case Quantity::Ppfd:             return Unit::Micromole;
    case Quantity::Battery:          return Unit::Percent;
    case Quantity::Co2:              return Unit::Ppm;
    case Quantity::Voc:              return Unit::Ppm;
    case Quantity::Pm25:             return Unit::MicrogramPerCubicMetre;
    case Quantity::Pm10:             return Unit::MicrogramPerCubicMetre;
    case Quantity::Hcho:             return Unit::MicrogramPerCubicMetre;
    case Quantity::Radioactivity:    return Unit::MicrosievertPerHour;
    case Quantity::WaterTank:        return Unit::Percent;
    case Quantity::Dli:              return Unit::None; // derived; never a stored reading
    }
    return Unit::None;
}

// One atomic measurement. "Absent" is std::nullopt — NEVER the -99 sentinel
// WatchFlower used.
struct Reading {
    Quantity quantity {};
    std::optional<double> value {};
    Unit unit { Unit::None };
    QDateTime timestamp {};
    Provenance provenance { Provenance::Unknown };
};

} // namespace klr
