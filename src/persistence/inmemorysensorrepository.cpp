// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemorysensorrepository.h"

#include <QtCore/QTimeZone>

#include <algorithm>

namespace klr {

SensorId InMemorySensorRepository::ensure(HandleKind kind, const QString &handleValue,
                                          const QString &model)
{
    if (const std::optional<Sensor> existing = findByHandle(kind, handleValue))
        return existing->id; // dedup on the handle — same physical sensor

    Sensor s;
    s.id = SensorId::generate();
    s.model = model;
    s.handleKind = kind;
    s.handleValue = handleValue;
    s.firstSeen = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
    m_byId.insert(s.id.value, s);
    return s.id;
}

void InMemorySensorRepository::add(const Sensor &sensor)
{
    // Upsert by id — QHash::insert replaces an existing entry, so re-adding the same
    // backup is a no-op (idempotent restore) and the original id is preserved.
    m_byId.insert(sensor.id.value, sensor);
}

std::optional<Sensor> InMemorySensorRepository::get(SensorId id) const
{
    const auto it = m_byId.constFind(id.value);
    if (it == m_byId.cend())
        return std::nullopt;
    return *it;
}

std::optional<Sensor> InMemorySensorRepository::findByHandle(HandleKind kind,
                                                             const QString &handleValue) const
{
    for (const Sensor &s : m_byId) {
        if (s.handleKind == kind && s.handleValue == handleValue)
            return s;
    }
    return std::nullopt;
}

void InMemorySensorRepository::remove(SensorId id)
{
    m_byId.remove(id.value); // readings/bindings are cleared by the SensorDeleter use-case
}

QList<Sensor> InMemorySensorRepository::all() const
{
    QList<Sensor> out = m_byId.values();
    std::sort(out.begin(), out.end(), [](const Sensor &a, const Sensor &b) {
        if (a.firstSeen != b.firstSeen)
            return a.firstSeen < b.firstSeen;
        return a.id.toString() < b.id.toString();
    });
    return out;
}

} // namespace klr
