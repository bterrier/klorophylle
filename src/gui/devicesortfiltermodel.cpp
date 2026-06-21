// SPDX-License-Identifier: GPL-3.0-or-later
#include "devicesortfiltermodel.h"

#include "discovereddevicesmodel.h"
#include "ids.h"
#include "isensorrepository.h"

namespace klr {

DeviceSortFilterModel::DeviceSortFilterModel(bool onlySupported, bool excludeRegistered,
                                             ISensorRepository *sensors, QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_onlySupported(onlySupported)
    , m_excludeRegistered(excludeRegistered)
    , m_sensors(sensors)
{
    setDynamicSortFilter(true);
    sort(0);
}

void DeviceSortFilterModel::refilter()
{
    invalidateFilter();
}

bool DeviceSortFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // Supported sensors first; within a group, stronger signal (higher RSSI) first.
    const bool ls = sourceModel()->data(left, DiscoveredDevicesModel::SupportedRole).toBool();
    const bool rs = sourceModel()->data(right, DiscoveredDevicesModel::SupportedRole).toBool();
    if (ls != rs)
        return ls; // a supported row sorts before an unsupported one

    const int lr = sourceModel()->data(left, DiscoveredDevicesModel::RssiRole).toInt();
    const int rr = sourceModel()->data(right, DiscoveredDevicesModel::RssiRole).toInt();
    return lr > rr;
}

bool DeviceSortFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);

    if (m_onlySupported
        && !sourceModel()->data(idx, DiscoveredDevicesModel::SupportedRole).toBool())
        return false;

    // Hide devices already in the `sensors` table — they live in the "Registered sensors"
    // section instead, so the live-scan list shows only new/unknown devices. The
    // desktop handle is a MAC, matching how pairing/ingest mint the sensor.
    if (m_excludeRegistered && m_sensors) {
        const QString id = sourceModel()->data(idx, DiscoveredDevicesModel::IdRole).toString();
        if (!id.isEmpty() && m_sensors->findByHandle(HandleKind::Mac, id).has_value())
            return false;
    }
    return true;
}

} // namespace klr
