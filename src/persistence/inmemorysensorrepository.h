// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "clock.h"
#include "isensorrepository.h"

#include <QtCore/QHash>
#include <QtCore/QUuid>

namespace klr {

// The test/fake sensor repository. Keeps the domain testable with no DB/QML. The
// injected Clock stamps firstSeen, so dedup + first-sight ordering are deterministic.
class InMemorySensorRepository final : public ISensorRepository {
public:
    explicit InMemorySensorRepository(const Clock &clock) : m_clock(clock) {}

    SensorId ensure(HandleKind kind, const QString &handleValue, const QString &model) override;
    void add(const Sensor &sensor) override;
    std::optional<Sensor> get(SensorId id) const override;
    std::optional<Sensor> findByHandle(HandleKind kind, const QString &handleValue) const override;
    QList<Sensor> all() const override;
    void remove(SensorId id) override;

private:
    const Clock &m_clock;
    QHash<QUuid, Sensor> m_byId; // keyed by SensorId::value
};

} // namespace klr
