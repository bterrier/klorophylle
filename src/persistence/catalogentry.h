// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>

#include <optional>

// One row of the bundled plant catalog (the legacy ~3400-plant CSV). A plain value
// type — read-only reference data, copied by value like Plant/Reading.
//
// The catalog's natural key is the botanical name (`key`): it is unique in the CSV
// and is what a Plant stores to associate itself with a species. The ideal-range
// fields are parsed now but not surfaced until care thresholds are wired; `nullopt` == the
// CSV left the cell blank (never a sentinel).
namespace klr {

struct CatalogEntry {
    QString key;          // botanical name (CSV "Plant name") — the catalog key
    QString commonName;   // English common name (may be empty)

    // Ideal care ranges. nullopt == absent in the source row.
    std::optional<double> soilMoistureMin;       // %
    std::optional<double> soilMoistureMax;
    std::optional<double> soilConductivityMin;   // µS/cm
    std::optional<double> soilConductivityMax;
    std::optional<double> soilPhMin;
    std::optional<double> soilPhMax;
    std::optional<double> temperatureMin;        // °C
    std::optional<double> temperatureMax;
    std::optional<double> humidityMin;           // %RH
    std::optional<double> humidityMax;
    std::optional<double> lightLuxMin;           // lux
    std::optional<double> lightLuxMax;
    std::optional<double> lightMmolMin;          // mol/m²/day (mmol)
    std::optional<double> lightMmolMax;

    bool operator==(const CatalogEntry &) const = default;
};

} // namespace klr
