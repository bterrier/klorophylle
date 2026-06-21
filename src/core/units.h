// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reading.h"

// Display-unit preferences and the PURE conversion seam (see docs/adr/0008).
// Readings are STORED canonically (°C, lux, hPa — see canonicalUnit()); a user's unit
// choice is applied only at the formatting/charting boundary, never to stored data.
// Conversions are pure, unit-tested functions here — no settings, no Qt Quick.
namespace klr {

// User-selectable display units. The default of each enum is the canonical unit, so a
// default-constructed DisplayUnits formats exactly like the canonical path.
enum class TemperatureUnit { Celsius, Fahrenheit };
enum class IlluminanceUnit { Lux, Micromole };
enum class PressureUnit { Hectopascal, InchHg, MmHg };

struct DisplayUnits {
    TemperatureUnit temperature { TemperatureUnit::Celsius };
    IlluminanceUnit illuminance { IlluminanceUnit::Lux };
    PressureUnit pressure { PressureUnit::Hectopascal };
};

// Convert a value from one unit to another. Only the conversions the app offers are
// defined; an unhandled pair returns the value unchanged (identity), as does from==to.
// Temperature is exact; lux->µmol is a documented daylight APPROXIMATION (PPFD depends
// on the light spectrum — see docs/adr/0008).
double convert(double value, Unit from, Unit to);

// The unit a quantity is displayed in given the user's preferences — the canonical unit
// unless a preference overrides it (temperature, illuminance, pressure).
Unit displayUnit(Quantity q, const DisplayUnits &u);

} // namespace klr
