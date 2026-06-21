// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemorysyncstaterepository.h"

namespace klr {

std::optional<QDateTime> InMemorySyncStateRepository::lastHistorySync(SensorId sensor) const
{
    const auto it = m_last.constFind(sensor.toString());
    if (it == m_last.cend())
        return std::nullopt;
    return *it;
}

void InMemorySyncStateRepository::setLastHistorySync(SensorId sensor, const QDateTime &when)
{
    m_last.insert(sensor.toString(), when);
}

} // namespace klr
