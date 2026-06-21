// SPDX-License-Identifier: GPL-3.0-or-later
#include "discovereddevicesmodel.h"

#include "blescanner.h"
#include "clock.h"
#include "discovereddevice.h"
#include "format.h"
#include "historysynccontroller.h"
#include "liveness.h"

#include <algorithm>

namespace klr {

namespace {

// The newest present decoded value's timestamp across a device's latest snapshot.
std::optional<qint64> newestValueMs(const DiscoveredDevice &d)
{
    std::optional<qint64> ms;
    for (const Reading &r : d.latest) {
        if (!r.value || !r.timestamp.isValid())
            continue;
        const qint64 t = r.timestamp.toMSecsSinceEpoch();
        ms = ms ? std::max(*ms, t) : t;
    }
    return ms;
}

} // namespace

DiscoveredDevicesModel::DiscoveredDevicesModel(BleScanner &scanner, const Clock &clock,
                                               QObject *parent)
    : QAbstractListModel(parent)
    , m_scanner(scanner)
    , m_clock(clock)
{
    connect(&m_scanner, &BleScanner::deviceAdded, this, &DiscoveredDevicesModel::onDeviceAdded);
    connect(&m_scanner, &BleScanner::deviceChanged, this, &DiscoveredDevicesModel::onDeviceChanged);
}

void DiscoveredDevicesModel::onDeviceAdded(const QString &id)
{
    beginInsertRows({}, int(m_ids.size()), int(m_ids.size()));
    m_ids.append(id);
    endInsertRows();
}

void DiscoveredDevicesModel::onDeviceChanged(const QString &id)
{
    const int row = int(m_ids.indexOf(id));
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row));
}

int DiscoveredDevicesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_ids.size());
}

QVariant DiscoveredDevicesModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_ids.size())
        return {};
    const DiscoveredDevice *d = m_scanner.device(m_ids.at(index.row()));
    if (!d)
        return {};
    switch (role) {
    case IdRole:         return d->id;
    case NameRole:       return d->name.isEmpty() ? d->id : d->name;
    case RssiRole:       return int(d->rssi);
    case ModelRole:      return d->model;
    case ValueCountRole: return int(d->latest.size());
    case SupportedRole:  return !d->model.isEmpty() || !d->latest.isEmpty();
    case LivenessRole:   return livenessForId(d->id);
    case LastSeenRole:   return formatAgo(d->lastSeen, m_clock.nowMs());
    case GattOpenRole:   return gattOpenForId(d->id);
    case BatteryRole: {
        for (const Reading &r : d->latest)
            if (r.quantity == Quantity::Battery && r.value)
                return formatValue(r);
        return QString();
    }
    default:             return {};
    }
}

int DiscoveredDevicesModel::livenessForId(const QString &id) const
{
    // A device is silent while we hold a GATT connection to it — show "connected" (blue), not
    // offline, for the row we are actually talking to.
    if (gattOpenForId(id))
        return kConnectivityConnected;
    const DiscoveredDevice *d = m_scanner.device(id);
    if (!d)
        return int(Liveness::Offline);
    const std::optional<qint64> lastBroadcast =
        d->lastSeen.isValid() ? std::optional<qint64>(d->lastSeen.toMSecsSinceEpoch()) : std::nullopt;
    return int(livenessOf(lastBroadcast, newestValueMs(*d), m_clock.nowMs()));
}

bool DiscoveredDevicesModel::gattOpenForId(const QString &id) const
{
    return id == m_scanner.currentGattId()
        || (m_historySync && id == m_historySync->activeId());
}

void DiscoveredDevicesModel::refreshConnectivity()
{
    for (int row = 0; row < m_ids.size(); ++row) {
        const QString &id = m_ids.at(row);
        const int live = livenessForId(id);
        const bool gatt = gattOpenForId(id);
        // -2 sentinel forces the first emit; thereafter only real changes propagate. An
        // actively-broadcasting device already refreshes via onDeviceChanged, so the timer
        // mainly drives the →Offline flip and GATT-open toggles.
        if (live == m_liveness.value(id, -2) && gatt == m_gattOpen.value(id, false))
            continue;
        m_liveness.insert(id, live);
        m_gattOpen.insert(id, gatt);
        const QModelIndex idx = index(row);
        Q_EMIT dataChanged(idx, idx, { LivenessRole, LastSeenRole, GattOpenRole, BatteryRole });
    }
}

QHash<int, QByteArray> DiscoveredDevicesModel::roleNames() const
{
    return {
        { IdRole, "deviceId" },
        { NameRole, "deviceName" },
        { RssiRole, "rssi" },
        { ModelRole, "model" },
        { ValueCountRole, "valueCount" },
        { SupportedRole, "supported" },
        { LivenessRole, "liveness" },
        { LastSeenRole, "lastSeen" },
        { BatteryRole, "battery" },
        { GattOpenRole, "gattOpen" },
    };
}

} // namespace klr
