// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iplantrepository.h"

#include <QtCore/QHash>
#include <QtCore/QUuid>

namespace klr {

// The test/fake plant repository. Keeps the domain testable with no DB/QML.
class InMemoryPlantRepository final : public IPlantRepository {
public:
    void add(const Plant &plant) override;
    void update(const Plant &plant) override;
    void remove(PlantId id) override;
    std::optional<Plant> get(PlantId id) const override;
    QList<Plant> all() const override;

private:
    QHash<QUuid, Plant> m_byId; // keyed by PlantId::value
};

} // namespace klr
