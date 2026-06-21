// SPDX-License-Identifier: GPL-3.0-or-later
#include "registeredsensorsmodel.h"

#include "binding.h"
#include "blescanner.h"
#include "clock.h"
#include "discovereddevice.h"
#include "format.h"
#include "historysynccontroller.h"
#include "ibindingrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "liveness.h"
#include "sensor.h"

#include <QtCore/QTimeZone>

#include <algorithm>

namespace klr {

RegisteredSensorsModel::RegisteredSensorsModel(ISensorRepository &sensors,
                                               IBindingRepository &bindings,
                                               IReadingRepository &readings, const Clock &clock,
                                               BleScanner *scanner,
                                               HistorySyncController *historySync, QObject *parent)
    : QAbstractListModel(parent)
    , m_sensors(sensors)
    , m_bindings(bindings)
    , m_readings(readings)
    , m_clock(clock)
    , m_scanner(scanner)
    , m_historySync(historySync)
{
}

void RegisteredSensorsModel::refresh()
{
    beginResetModel();
    m_rows.clear();
    for (const Sensor &s : m_sensors.all()) {
        Row row;
        row.sensor = s.id;
        row.model = s.model;
        row.address = s.handleValue;
        // Bound to a plant iff ANY binding (open or closed) references this sensor: its
        // readings then belong to that plant's history and it cannot be deleted (a closed
        // binding still counts — detaching does not remove it; only deleting the plant does).
        row.bound = !m_bindings.bindingsForSensor(s.id).isEmpty();
        // Authoritative battery: from the reading store, so it shows even for connect-only
        // sensors (Flower Care never broadcasts battery — history sync fills it).
        if (const std::optional<Reading> bat = m_readings.latest(s.id, Quantity::Battery);
            bat && bat->value)
            row.battery = formatValue(*bat);
        row.liveness = livenessForHandle(row.address);
        row.gattOpen = gattOpenForHandle(row.address);
        m_rows.append(row);
    }
    endResetModel();
}

int RegisteredSensorsModel::livenessForHandle(const QString &handle) const
{
    if (handle.isEmpty() || !m_scanner)
        return -1; // not judgeable (no handle / no BLE wired) -> no dot
    // While a GATT connection is open the sensor stops advertising — show "connected".
    if (gattOpenForHandle(handle))
        return kConnectivityConnected;
    const DiscoveredDevice *d = m_scanner->device(handle);
    if (!d)
        return int(Liveness::Offline); // registered but never heard this session
    const std::optional<qint64> lastBroadcast =
        d->lastSeen.isValid() ? std::optional<qint64>(d->lastSeen.toMSecsSinceEpoch()) : std::nullopt;
    std::optional<qint64> lastValue;
    for (const Reading &r : d->latest) {
        if (!r.value || !r.timestamp.isValid())
            continue;
        const qint64 t = r.timestamp.toMSecsSinceEpoch();
        lastValue = lastValue ? std::max(*lastValue, t) : t;
    }
    return int(livenessOf(lastBroadcast, lastValue, m_clock.nowMs()));
}

bool RegisteredSensorsModel::gattOpenForHandle(const QString &handle) const
{
    if (handle.isEmpty())
        return false;
    return (m_scanner && handle == m_scanner->currentGattId())
        || (m_historySync && handle == m_historySync->activeId());
}

int RegisteredSensorsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant RegisteredSensorsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case SensorIdRole: return r.sensor.toString();
    case ModelRole:    return r.model;
    case AddressRole:  return r.address;
    case BoundRole:    return r.bound;
    case BatteryRole:  return r.battery;
    case LivenessRole: return livenessForHandle(r.address);
    case GattOpenRole: return gattOpenForHandle(r.address);
    case LastSeenRole: {
        const DiscoveredDevice *d = m_scanner ? m_scanner->device(r.address) : nullptr;
        return d ? formatAgo(d->lastSeen, m_clock.nowMs()) : QString();
    }
    default:           return {};
    }
}

QHash<int, QByteArray> RegisteredSensorsModel::roleNames() const
{
    return {
        { SensorIdRole, "sensorId" },
        { ModelRole, "model" },
        { AddressRole, "address" },
        { BoundRole, "bound" },
        { LivenessRole, "liveness" },
        { BatteryRole, "battery" },
        { LastSeenRole, "lastSeen" },
        { GattOpenRole, "gattOpen" },
    };
}

void RegisteredSensorsModel::refreshConnectivity()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const int live = livenessForHandle(m_rows.at(i).address);
        const bool gatt = gattOpenForHandle(m_rows.at(i).address);
        if (live == m_rows.at(i).liveness && gatt == m_rows.at(i).gattOpen)
            continue;
        m_rows[i].liveness = live;
        m_rows[i].gattOpen = gatt;
        const QModelIndex idx = index(i);
        Q_EMIT dataChanged(idx, idx, { LivenessRole, LastSeenRole, GattOpenRole });
    }
}

} // namespace klr
