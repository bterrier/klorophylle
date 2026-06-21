// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemorycarethresholdrepository.h"

namespace klr {

QList<CareRange> InMemoryCareThresholdRepository::thresholdsFor(PlantId plant) const
{
    QList<CareRange> out;
    for (const Row &r : m_rows) {
        if (r.plant == plant)
            out.append(r.range);
    }
    return out;
}

void InMemoryCareThresholdRepository::setRange(PlantId plant, const CareRange &range)
{
    // Upsert by (plant, quantity); an unset range removes the row.
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].plant == plant && m_rows[i].range.quantity == range.quantity) {
            if (range.isSet())
                m_rows[i].range = range;
            else
                m_rows.removeAt(i);
            return;
        }
    }
    if (range.isSet())
        m_rows.append({ plant, range });
}

void InMemoryCareThresholdRepository::replaceAll(PlantId plant, std::span<const CareRange> ranges)
{
    clear(plant);
    for (const CareRange &r : ranges) {
        if (r.isSet())
            m_rows.append({ plant, r });
    }
}

void InMemoryCareThresholdRepository::clear(PlantId plant)
{
    m_rows.removeIf([&](const Row &r) { return r.plant == plant; });
}

} // namespace klr
