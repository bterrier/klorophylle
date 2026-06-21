// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogthresholds.h"

#include "icatalogrepository.h"

#include <algorithm>

namespace klr {

namespace {

// Append a CareRange for `q` if either bound is present (an all-nullopt pair judges
// nothing, so it is not worth a row).
void addRange(QList<CareRange> &out, Quantity q, std::optional<double> min,
              std::optional<double> max)
{
    if (min.has_value() || max.has_value())
        out.append(CareRange{ q, min, max });
}

} // namespace

QList<CareRange> idealRanges(const CatalogEntry &e)
{
    QList<CareRange> ranges;
    addRange(ranges, Quantity::SoilMoisture, e.soilMoistureMin, e.soilMoistureMax);
    addRange(ranges, Quantity::SoilConductivity, e.soilConductivityMin, e.soilConductivityMax);
    addRange(ranges, Quantity::AirTemperature, e.temperatureMin, e.temperatureMax);
    addRange(ranges, Quantity::AirHumidity, e.humidityMin, e.humidityMax);
    // Light is seeded on BOTH columns: the lux range stays only as a display range for
    // the metric bar, while the DLI verdict is judged on the mmol·m⁻²·day⁻¹ column — the
    // horticulturally correct "is it getting enough light over the day?" measure.
    addRange(ranges, Quantity::Illuminance, e.lightLuxMin, e.lightLuxMax);
    addRange(ranges, Quantity::Dli, e.lightMmolMin, e.lightMmolMax);
    return ranges;
}

QList<CareRange> mergeRanges(const QList<CareRange> &ideals, const QList<CareRange> &overrides)
{
    QList<CareRange> out = ideals;
    for (const CareRange &o : overrides) {
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const CareRange &r) { return r.quantity == o.quantity; });
        if (it != out.end())
            *it = o; // override wins for this quantity
        else
            out.append(o);
    }
    return out;
}

QList<CareRange> effectiveRanges(const ICatalogRepository *catalog, const QString &species,
                                 const QList<CareRange> &overrides)
{
    QList<CareRange> ideals;
    if (catalog && !species.isEmpty()) {
        if (const std::optional<CatalogEntry> e = catalog->byKey(species))
            ideals = idealRanges(*e);
    }
    return mergeRanges(ideals, overrides);
}

} // namespace klr
