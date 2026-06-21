// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "isyncstaterepository.h"

#include <QtCore/QHash>

namespace klr {

// The test/fake history-sync-state repository: a sensor-id -> last-sync map, the same behaviour the
// SQLite impl gives, so both pass the shared suite.
class InMemorySyncStateRepository final : public ISyncStateRepository {
public:
    std::optional<QDateTime> lastHistorySync(SensorId sensor) const override;
    void setLastHistorySync(SensorId sensor, const QDateTime &when) override;

private:
    QHash<QString, QDateTime> m_last; // sensorId string -> last completed sync
};

} // namespace klr
