// SPDX-License-Identifier: GPL-3.0-or-later
#include "units.h"

namespace klr {

double convert(double value, Unit from, Unit to)
{
    if (from == to)
        return value;

    switch (from) {
    case Unit::DegreeCelsius:
        if (to == Unit::DegreeFahrenheit)
            return value * 1.8 + 32.0;
        break;
    case Unit::DegreeFahrenheit:
        if (to == Unit::DegreeCelsius)
            return (value - 32.0) / 1.8;
        break;
    case Unit::Lux:
        // Daylight approximation: PPFD (µmol·m⁻²·s⁻¹) ≈ lux × 0.0185. Spectrum-dependent
        // (see docs/adr/0008) — good enough for an at-a-glance display, not a lab figure.
        if (to == Unit::Micromole)
            return value * 0.0185;
        break;
    case Unit::Micromole:
        if (to == Unit::Lux)
            return value / 0.0185;
        break;
    case Unit::Hectopascal:
        if (to == Unit::InchOfMercury)
            return value * 0.0295299830714;
        if (to == Unit::MillimetreOfMercury)
            return value * 0.750061682704;
        break;
    case Unit::InchOfMercury:
        if (to == Unit::Hectopascal)
            return value / 0.0295299830714;
        break;
    case Unit::MillimetreOfMercury:
        if (to == Unit::Hectopascal)
            return value / 0.750061682704;
        break;
    default:
        break;
    }
    return value; // no conversion defined for this pair — leave unchanged
}

Unit displayUnit(Quantity q, const DisplayUnits &u)
{
    switch (q) {
    case Quantity::AirTemperature:
    case Quantity::SoilTemperature:
        return u.temperature == TemperatureUnit::Fahrenheit ? Unit::DegreeFahrenheit
                                                            : Unit::DegreeCelsius;
    case Quantity::Illuminance:
        return u.illuminance == IlluminanceUnit::Micromole ? Unit::Micromole : Unit::Lux;
    case Quantity::Pressure:
        switch (u.pressure) {
        case PressureUnit::InchHg: return Unit::InchOfMercury;
        case PressureUnit::MmHg:   return Unit::MillimetreOfMercury;
        case PressureUnit::Hectopascal: break;
        }
        return Unit::Hectopascal;
    default:
        return canonicalUnit(q);
    }
}

} // namespace klr
