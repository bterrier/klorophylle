// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"
#include "plant.h"

#include <QtCore/QList>
#include <optional>

// The repository boundary: the ONLY code that touches storage lives behind this
// The domain/UI never see SQL.
//
// SqlitePlantRepository implements this; tests run the same behavioural suite
// against InMemoryPlantRepository and SqlitePlantRepository.
namespace klr {

class IPlantRepository {
public:
    virtual ~IPlantRepository() = default;

    virtual void add(const Plant &plant) = 0;
    virtual void update(const Plant &plant) = 0;
    virtual void remove(PlantId id) = 0;
    virtual std::optional<Plant> get(PlantId id) const = 0;
    virtual QList<Plant> all() const = 0;
};

} // namespace klr
