// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemoryplantrepository.h"

#include <algorithm>

namespace klr {

void InMemoryPlantRepository::add(const Plant &plant)
{
    m_byId.insert(plant.id.value, plant);
}

void InMemoryPlantRepository::update(const Plant &plant)
{
    m_byId.insert(plant.id.value, plant);
}

void InMemoryPlantRepository::remove(PlantId id)
{
    m_byId.remove(id.value);
}

std::optional<Plant> InMemoryPlantRepository::get(PlantId id) const
{
    const auto it = m_byId.constFind(id.value);
    if (it == m_byId.cend())
        return std::nullopt;
    return *it;
}

QList<Plant> InMemoryPlantRepository::all() const
{
    QList<Plant> out = m_byId.values();
    // Deterministic order: by trackedSince, then id, so tests don't depend on hashing.
    std::sort(out.begin(), out.end(), [](const Plant &a, const Plant &b) {
        if (a.trackedSince != b.trackedSince)
            return a.trackedSince < b.trackedSince;
        return a.id.toString() < b.id.toString();
    });
    return out;
}

} // namespace klr
