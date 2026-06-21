// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "icarethresholdrepository.h"

namespace klr {

// The test/fake care-threshold repository. A flat row list (plant, range), upserted
// per (plant, quantity) — the same behaviour the SQLite impl gives, so both pass the
// shared suite.
class InMemoryCareThresholdRepository final : public ICareThresholdRepository {
public:
    QList<CareRange> thresholdsFor(PlantId plant) const override;
    void setRange(PlantId plant, const CareRange &range) override;
    void replaceAll(PlantId plant, std::span<const CareRange> ranges) override;
    void clear(PlantId plant) override;

private:
    struct Row {
        PlantId plant;
        CareRange range;
    };
    QList<Row> m_rows;
};

} // namespace klr
