// SPDX-License-Identifier: GPL-3.0-or-later
#include "plantlistmodel.h"

#include "binding.h"
#include "blescanner.h"
#include "careevaluation.h"
#include "catalogthresholds.h"
#include "clock.h"
#include "discovereddevice.h"
#include "format.h"
#include "ibindingrepository.h"
#include "icarethresholdrepository.h"
#include "icatalogrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "liveness.h"
#include "sensor.h"
#include "settingsstore.h"
#include "units.h"

#include <QtCore/QLocale>
#include <QtCore/QTimeZone>
#include <QtCore/QVariantMap>

#include <algorithm>
#include <span>

namespace klr {

PlantListModel::PlantListModel(IPlantRepository &repo, QObject *parent)
    : QAbstractListModel(parent)
    , m_repo(repo)
{
    refresh();
}

PlantListModel::PlantListModel(IPlantRepository &repo, ISensorRepository *sensors,
                               IBindingRepository *bindings, IReadingRepository *readings,
                               ICareThresholdRepository *thresholds,
                               const ICatalogRepository *catalog, const Clock *clock,
                               const SettingsStore *settings, BleScanner *scanner, QObject *parent)
    : QAbstractListModel(parent)
    , m_repo(repo)
    , m_sensors(sensors)
    , m_bindings(bindings)
    , m_readings(readings)
    , m_thresholds(thresholds)
    , m_catalog(catalog)
    , m_settings(settings)
    , m_clock(clock)
    , m_scanner(scanner)
{
    refresh();
}

// The current value for one quantity rendered as a card metric: formatted value text
// (display-unit aware when a SettingsStore is injected) + the value's 0..1 position in
// the plant's ideal range. `light` may arrive as Illuminance or Ppfd — the caller picks.
PlantListModel::Metric PlantListModel::metricOf(const Reading *r,
                                                std::span<const CareRange> ranges) const
{
    Metric m;
    if (!r || !r->value)
        return m; // no current reading (or no value) for this quantity — card hides it
    m.present = true;
    m.valueText = m_settings ? formatValue(*r, m_settings->displayUnits()) : formatValue(*r);
    const std::optional<CareRange> range = rangeFor(ranges, r->quantity);
    if (range && range->min && range->max && *range->max > *range->min) {
        const double f = (*r->value - *range->min) / (*range->max - *range->min);
        m.fraction = std::clamp(f, 0.0, 1.0);
        m.hasRange = true;
    }
    return m;
}

PlantListModel::RowCare PlantListModel::careOf(const Plant &p) const
{
    RowCare care;
    if (!m_bindings || !m_readings || !m_thresholds || !m_clock)
        return care;
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock->nowMs(), QTimeZone::UTC);
    // The binding/reading/range fetch + per-quantity dispatch is shared with the care alert
    // evaluator (careevaluation.h) — this model only adds the home-card metrics + connectivity.
    const PlantCareSnapshot snap =
        evaluatePlantCare(p, *m_bindings, *m_readings, *m_thresholds, m_catalog, now);

    // Cache the active sensors' BLE handles so the timer-driven refreshConnectivity() can
    // judge liveness without re-hitting the repositories. -1 (no dot) until we know better.
    if (m_sensors) {
        for (const PlantSensorBinding &b : m_bindings->activeFor(p.id, now)) {
            if (const std::optional<Sensor> s = m_sensors->get(b.sensor); s && !s->handleValue.isEmpty())
                care.handles.append(s->handleValue);
        }
    }
    care.connectivity = connectivityOf(care.handles);

    // The two metrics the home card surfaces, independent of whether thresholds exist.
    const std::span<const CareRange> rangeSpan(snap.ranges.constData(), snap.ranges.size());
    auto find = [&snap](Quantity q) -> const Reading * {
        for (const Reading &r : snap.current)
            if (r.quantity == q)
                return &r;
        return nullptr;
    };
    const Reading *moisture = find(Quantity::SoilMoisture);
    const Reading *light = find(Quantity::Illuminance);
    if (!light)
        light = find(Quantity::Ppfd);
    care.moisture = metricOf(moisture, rangeSpan);
    care.light = metricOf(light, rangeSpan);
    care.level = snap.level;
    return care;
}

int PlantListModel::connectivityOf(const QStringList &handles) const
{
    if (handles.isEmpty() || !m_scanner || !m_clock)
        return -1; // no sensor bound (or no BLE/clock wired) — the card shows no dot
    // A live GATT connection to one of this plant's sensors wins outright: the device goes off the
    // air during a read/history download, so judging it on broadcasts would (wrongly) show offline.
    if (const QString active = m_scanner->activeGattId();
        !active.isEmpty() && handles.contains(active))
        return kConnectivityConnected;
    // The freshest bound sensor wins: aggregate the newest last-seen and the newest decoded
    // value across all the plant's handles, then judge once. (Both come from the scanner's
    // in-memory snapshot — last broadcast vs last usable value — no repository round-trip.)
    std::optional<qint64> lastBroadcastMs;
    std::optional<qint64> lastValueMs;
    for (const QString &h : handles) {
        const DiscoveredDevice *d = m_scanner->device(h);
        if (!d)
            continue;
        if (d->lastSeen.isValid()) {
            const qint64 ms = d->lastSeen.toMSecsSinceEpoch();
            lastBroadcastMs = lastBroadcastMs ? std::max(*lastBroadcastMs, ms) : ms;
        }
        for (const Reading &r : d->latest) {
            if (!r.value || !r.timestamp.isValid())
                continue;
            const qint64 ms = r.timestamp.toMSecsSinceEpoch();
            lastValueMs = lastValueMs ? std::max(*lastValueMs, ms) : ms;
        }
    }
    return int(livenessOf(lastBroadcastMs, lastValueMs, m_clock->nowMs()));
}

void PlantListModel::refresh()
{
    beginResetModel();
    m_rows = m_repo.all();
    m_care.resize(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i)
        m_care[i] = careOf(m_rows.at(i));
    endResetModel();
}

void PlantListModel::refreshConnectivity()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const int conn = connectivityOf(m_care.at(i).handles);
        if (conn == m_care.at(i).connectivity)
            continue; // unchanged — most ticks emit nothing
        m_care[i].connectivity = conn;
        const QModelIndex idx = index(i);
        Q_EMIT dataChanged(idx, idx, { ConnectivityRole });
    }
}

int PlantListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PlantListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Plant &p = m_rows.at(index.row());
    const RowCare &care = m_care.at(index.row());
    switch (role) {
    case PlantIdRole:      return p.id.toString();
    case DisplayNameRole:  return p.displayName;
    case SpeciesRole:      return p.species;
    case TrackedSinceRole: return QLocale().toString(p.trackedSince.toLocalTime(),
                                                     QLocale::ShortFormat);
    case HealthRole:       return int(care.level);
    case MoistureRole:     return metricMap(care.moisture);
    case LightRole:        return metricMap(care.light);
    case ConnectivityRole: return care.connectivity;
    default:               return {};
    }
}

QVariantMap PlantListModel::metricMap(const Metric &m)
{
    return {
        { QStringLiteral("present"), m.present },
        { QStringLiteral("valueText"), m.valueText },
        { QStringLiteral("fraction"), m.fraction },
        { QStringLiteral("hasRange"), m.hasRange },
    };
}

QHash<int, QByteArray> PlantListModel::roleNames() const
{
    return {
        { PlantIdRole, "plantId" },
        { DisplayNameRole, "displayName" },
        { SpeciesRole, "species" },
        { TrackedSinceRole, "trackedSince" },
        { HealthRole, "health" },
        { MoistureRole, "moisture" },
        { LightRole, "light" },
        { ConnectivityRole, "connectivity" },
    };
}

} // namespace klr
