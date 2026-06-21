// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reading.h"
#include "units.h"
#include <QtCore/QString>

// Presentation logic lives in unit-tested C++, NOT in untyped QML/JS
// QML binds to the results via model roles.
namespace klr {

QString label(Quantity q);             // e.g. "Soil moisture"
QString unitSymbol(Unit u);            // e.g. "%", "°C", "lux"

// The display unit symbol for the Daily Light Integral. Quantity::Dli is a derived
// dose with no Unit enumerator, so its symbol lives here — shared by the care readout and
// the thresholds editor so they always agree.
QString dliUnitSymbol();               // "mmol/m²/day"
QString formatValue(const Reading &r); // canonical units; e.g. "42.0 %", "—" when absent

// As above, but converted to the user's display units: a reading stored as °C is
// shown "71.6 °F" when DisplayUnits selects Fahrenheit. Storage stays canonical — only
// the displayed value/symbol change. formatValue(r) == formatValue(r, {}) (canonical).
QString formatValue(const Reading &r, const DisplayUnits &units);

// A short "time since" label for `t` relative to nowMs (epoch ms): "just now", "12s ago",
// "5m ago", "3h ago", else the localized short date. Empty when `t` is invalid. Used by the
// connectivity surfaces to show when a sensor was last heard. Clock injected (nowMs passed).
QString formatAgo(const QDateTime &t, qint64 nowMs);

} // namespace klr
