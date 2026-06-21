// SPDX-License-Identifier: GPL-3.0-or-later
#include "livereadingsmodel.h"

#include "blescanner.h"
#include "discovereddevice.h"
#include "format.h"
#include "settingsstore.h"
#include "units.h"

namespace klr {

LiveReadingsModel::LiveReadingsModel(BleScanner &scanner, const SettingsStore &settings,
                                     QObject *parent)
    : QAbstractListModel(parent)
    , m_scanner(scanner)
    , m_settings(settings)
{
    connect(&m_scanner, &BleScanner::deviceChanged, this, [this](const QString &id) {
        if (id == m_id)
            refresh();
    });
    // A unit-preference change re-formats every row's value + unit (storage unchanged).
    connect(&m_settings, &SettingsStore::unitsChanged, this, [this] {
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(int(m_rows.size()) - 1), {ValueTextRole, UnitRole});
    });
}

void LiveReadingsModel::setDeviceId(const QString &id)
{
    m_id = id;
    refresh();
}

void LiveReadingsModel::refresh()
{
    beginResetModel();
    const DiscoveredDevice *d = m_scanner.device(m_id);
    m_rows = d ? d->latest : QList<Reading> {};
    endResetModel();
}

int LiveReadingsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant LiveReadingsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Reading &r = m_rows.at(index.row());
    switch (role) {
    case QuantityRole:  return int(r.quantity);
    case LabelRole:     return label(r.quantity);
    case ValueTextRole: return formatValue(r, m_settings.displayUnits());
    case UnitRole:      return unitSymbol(displayUnit(r.quantity, m_settings.displayUnits()));
    case PresentRole:   return r.value.has_value();
    default:            return {};
    }
}

QHash<int, QByteArray> LiveReadingsModel::roleNames() const
{
    return {
        { QuantityRole, "quantity" },
        { LabelRole, "label" },
        { ValueTextRole, "valueText" },
        { UnitRole, "unit" },
        { PresentRole, "present" },
    };
}

} // namespace klr
