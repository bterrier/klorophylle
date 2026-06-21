// SPDX-License-Identifier: GPL-3.0-or-later
#include "plantcaremodel.h"

#include "catalogthresholds.h"
#include "clock.h"
#include "format.h"
#include "ibindingrepository.h"
#include "icarethresholdrepository.h"
#include "icatalogrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "log.h"
#include "plant.h"
#include "settingsstore.h"
#include "storageerror.h"
#include "units.h"

#include <QtCore/QLocale>
#include <QtCore/QTimeZone>
#include <QtCore/QVariantMap>

#include <algorithm>

namespace klr {

PlantCareModel::PlantCareModel(ISensorRepository &sensors, IBindingRepository &bindings,
                               IReadingRepository &readings, ICareThresholdRepository &thresholds,
                               const Clock &clock, const SettingsStore &settings,
                               const IPlantRepository *plants, const ICatalogRepository *catalog,
                               QObject *parent)
    : QAbstractListModel(parent)
    , m_sensors(sensors)
    , m_bindings(bindings)
    , m_readings(readings)
    , m_thresholds(thresholds)
    , m_plants(plants)
    , m_catalog(catalog)
    , m_clock(clock)
    , m_settings(settings)
{
    // A unit-preference change re-formats current readings and re-converts any open
    // history chart (storage stays canonical — only the displayed/charted values change).
    connect(&m_settings, &SettingsStore::unitsChanged, this, [this] {
        refresh();
        if (m_historyQuantity)
            loadHistory(*m_historyQuantity);
    });
}

QDateTime PlantCareModel::nowUtc() const
{
    return QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
}

QList<PlantSensorBinding> PlantCareModel::activeBindingsNow() const
{
    if (!m_plant)
        return {};
    return m_bindings.activeFor(*m_plant, nowUtc());
}

void PlantCareModel::setPlant(std::optional<PlantId> plant)
{
    m_plant = plant;
    refresh();
}

QList<CareRange> PlantCareModel::effectiveRangesForPlant() const
{
    if (!m_plant)
        return {};
    QString species;
    if (m_plants) {
        if (const std::optional<Plant> p = m_plants->get(*m_plant))
            species = p->species;
    }
    // Effective ranges = the species' catalog ideals overlaid with per-plant overrides.
    // No seeding: the species + catalog are enough to judge, resolved live (ADR 0003).
    return effectiveRanges(m_catalog, species, m_thresholds.thresholdsFor(*m_plant));
}

void PlantCareModel::refresh()
{
    beginResetModel();
    const QList<PlantSensorBinding> active = activeBindingsNow();
    m_current = m_plant ? m_readings.currentForPlant(
                              std::span<const PlantSensorBinding>(active.constData(), active.size()))
                        : QList<Reading>{};
    // Battery is a property of the SENSOR, not the plant — it is surfaced per-sensor
    // (the Sensors tab / sensor-detail page), never as a plant-care reading row. It has
    // no CareRange, so dropping it here cannot change the health rollup.
    m_current.removeIf([](const Reading &r) { return r.quantity == Quantity::Battery; });
    // Cache the plant's effective ranges so the per-row status (and the chart band) judge
    // against them without a repo hit per data() call.
    m_ranges = effectiveRangesForPlant();
    appendDailyLightRow(); // the "Daily light" dose row, after the measured readings
    endResetModel();
}

void PlantCareModel::appendDailyLightRow()
{
    const std::span<const CareRange> rangeSpan(m_ranges.constData(), m_ranges.size());
    if (!rangeFor(rangeSpan, Quantity::Dli))
        return; // no dose range to judge against — no "Daily light" row
    // Only when a light sensor is actually bound (an Illuminance/PPFD current reading).
    std::optional<Quantity> lightQ;
    for (const Reading &r : m_current) {
        if (judgedOnDailyIntegral(r.quantity)) {
            lightQ = r.quantity;
            break;
        }
    }
    if (!lightQ)
        return;
    const QDateTime now = nowUtc();
    const QList<Reading> window = windowReadings(*lightQ, now.addDays(-(kDliWindowDays + 1)), now);
    const std::optional<double> dose = meanDailyLightIntegral(
        std::span<const Reading>(window.constData(), window.size()), now);
    // value == nullopt when no full day exists yet → shown as "—", judged Unknown.
    m_current.append(Reading{ Quantity::Dli, dose, Unit::None, now, Provenance::History });
}

CareStatus PlantCareModel::statusOf(const Reading &r) const
{
    // The instantaneous light reading is shown for reference only (value + metric bar); its
    // health verdict is carried by the synthesized "Daily light" (DLI) row, so it is not
    // double-judged here. Everything else routes through the shared klr_core dispatch,
    // which this model feeds with a recent-window fetch over its bindings (ADR 0009).
    if (judgedOnDailyIntegral(r.quantity))
        return CareStatus::Unknown;
    return statusForReading(
        r, std::span<const CareRange>(m_ranges.constData(), m_ranges.size()), nowUtc(),
        [this](Quantity q, const QDateTime &from, const QDateTime &to) {
            return windowReadings(q, from, to);
        });
}

QList<Reading> PlantCareModel::windowReadings(Quantity q, const QDateTime &from,
                                              const QDateTime &to) const
{
    if (!m_plant)
        return {};
    const QList<PlantSensorBinding> all = m_bindings.bindings(*m_plant);
    return m_readings.seriesForPlant(
        std::span<const PlantSensorBinding>(all.constData(), all.size()), q, from, to);
}

void PlantCareModel::loadHistory(Quantity quantity)
{
    m_historyQuantity = quantity; // remembered so a unit-preference change can re-convert
    if (!m_plant) {
        m_history.clear();
        return;
    }
    // Every binding (open AND closed) so the series follows the plant across swaps.
    const QList<PlantSensorBinding> all = m_bindings.bindings(*m_plant);
    const QDateTime to = nowUtc();
    const QDateTime from = to.addDays(-3650); // effectively all history, clipped by windows
    QList<Reading> series = m_readings.seriesForPlant(
        std::span<const PlantSensorBinding>(all.constData(), all.size()), quantity, from, to);

    // Convert canonical samples to the user's display unit so the QtGraphs axis renders
    // in that unit (the series itself stays unit-agnostic). Storage is untouched.
    const Unit disp = displayUnit(quantity, m_settings.displayUnits());
    for (Reading &r : series) {
        if (r.value) {
            r.value = convert(*r.value, r.unit, disp);
            r.unit = disp;
        }
    }

    // The ideal-range band, converted to the same display unit as the series so
    // the chart shades the healthy zone. Absent when the plant has no range for it.
    const QList<CareRange> ranges = effectiveRangesForPlant();
    std::optional<CareRange> band =
        rangeFor(std::span<const CareRange>(ranges.constData(), ranges.size()), quantity);
    if (band) {
        const Unit canon = canonicalUnit(quantity);
        if (band->min)
            band->min = convert(*band->min, canon, disp);
        if (band->max)
            band->max = convert(*band->max, canon, disp);
    }
    m_history.setReadings(series, band);
}

int PlantCareModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_current.size());
}

// The reading's 0..1 position in its ideal range, for a metric bar — present only when
// the value exists and the range is both-bounded (min < max). Value and range bounds are
// both canonical, so the ratio is unit-independent (no conversion). Mirrors
// PlantListModel::metricOf (the Plants-home cards use the same gradient bar).
std::optional<double> PlantCareModel::fractionOf(const Reading &r) const
{
    if (!r.value)
        return std::nullopt;
    const std::optional<CareRange> range =
        rangeFor(std::span<const CareRange>(m_ranges.constData(), m_ranges.size()), r.quantity);
    if (!range || !range->min || !range->max || *range->max <= *range->min)
        return std::nullopt;
    return std::clamp((*r.value - *range->min) / (*range->max - *range->min), 0.0, 1.0);
}

QVariant PlantCareModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_current.size())
        return {};
    const Reading &r = m_current.at(index.row());
    switch (role) {
    case QuantityRole:  return int(r.quantity);
    case LabelRole:     return label(r.quantity);
    case ValueTextRole: return formatValue(r, m_settings.displayUnits());
    case UnitRole:      return unitSymbol(displayUnit(r.quantity, m_settings.displayUnits()));
    case PresentRole:   return r.value.has_value();
    case StatusRole:    return int(statusOf(r));
    case FractionRole:  return fractionOf(r).value_or(0.0);
    case HasRangeRole:  return fractionOf(r).has_value();
    default:            return {};
    }
}

QHash<int, QByteArray> PlantCareModel::roleNames() const
{
    return {
        { QuantityRole, "quantity" },
        { LabelRole, "label" },
        { ValueTextRole, "valueText" },
        { UnitRole, "unit" },
        { PresentRole, "present" },
        { StatusRole, "status" },
        { FractionRole, "fraction" },
        { HasRangeRole, "hasRange" },
    };
}

QVariantList PlantCareModel::boundSensors() const
{
    QVariantList out;
    for (const PlantSensorBinding &b : activeBindingsNow()) {
        const std::optional<Sensor> s = m_sensors.get(b.sensor);
        QVariantMap m;
        m.insert(QStringLiteral("sensorId"), b.sensor.toString());
        m.insert(QStringLiteral("model"), s ? s->model : QString());
        // The raw BLE handle (MAC on desktop/Android), so two same-model sensors are
        // distinguishable in the UI (they otherwise render identically).
        m.insert(QStringLiteral("address"), s ? s->handleValue : QString());
        m.insert(QStringLiteral("since"),
                 QLocale().toString(b.validFrom.toLocalTime(), QLocale::ShortFormat));
        m.insert(QStringLiteral("role"), b.role.has_value() ? label(*b.role) : QString());
        out.append(m);
    }
    return out;
}

void PlantCareModel::attach(HandleKind kind, const QString &handleValue, const QString &model,
                            std::span<const Reading> snapshot)
{
    if (!m_plant)
        return;
    try {
        const SensorId sensor = m_sensors.ensure(kind, handleValue, model);
        m_bindings.bind(*m_plant, sensor, nowUtc(), std::nullopt); // no role: redundant probes allowed
        if (!snapshot.empty())
            m_readings.append(sensor, snapshot);
    } catch (const StorageError &e) {
        qCWarning(lcApp) << "attach sensor failed:" << e.what();
    }
    refresh();
}

void PlantCareModel::detach(SensorId sensor)
{
    if (!m_plant)
        return;
    try {
        m_bindings.unbind(*m_plant, sensor, nowUtc());
    } catch (const StorageError &e) {
        qCWarning(lcApp) << "detach sensor failed:" << e.what();
    }
    refresh();
}

} // namespace klr
