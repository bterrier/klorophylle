// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "binding.h"    // PlantSensorBinding
#include "carestatus.h" // CareRange / CareStatus
#include "ids.h"
#include "reading.h"
#include "sensor.h"      // HandleKind
#include "seriesmodel.h" // history chart view-model

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QVariantList>
#include <optional>
#include <span>

namespace klr {

class ISensorRepository;
class IBindingRepository;
class IReadingRepository;
class ICareThresholdRepository;
class ICatalogRepository;
class IPlantRepository;
class Clock;
class SettingsStore;

// The per-plant care aggregate (the analogue of WatchFlower's MonitoredPlant): it
// joins the selected plant's active sensor bindings with their readings and exposes,
// as a thin QML model, ONE current value per quantity — resolved through the bindings
// (history follows the plant; multi-sensor collapses NewestWins with explicit-role
// preference; ADR 0005). It also owns the attach/detach/ingest actions so every
// binding timestamp comes from ONE injected clock. No SQL, no setContextProperty —
// AppContext hands this to QML.
//
// Per-quantity care STATUS: each current reading is classified against the
// plant's ideal range (ICareThresholdRepository, seeded from the catalog species but
// overridable). evaluate() is pure (klr_core); the status is judged on the canonical
// stored value, independent of the display-unit preference. The StatusRole feeds the
// reading colour, and loadHistory() bands the chart with the ideal range (ADR 0009).
class PlantCareModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        QuantityRole = Qt::UserRole + 1,
        LabelRole,
        ValueTextRole, // formatted "42.0 %" / "—"
        UnitRole,
        PresentRole,
        StatusRole,    // CareStatus (int): TooLow / Ideal / TooHigh / Unknown
        FractionRole,  // 0..1 position of the value in the ideal range (0 when none)
        HasRangeRole,  // whether a both-bounded range exists to drive a metric bar
    };

    PlantCareModel(ISensorRepository &sensors, IBindingRepository &bindings,
                   IReadingRepository &readings, ICareThresholdRepository &thresholds,
                   const Clock &clock, const SettingsStore &settings,
                   const IPlantRepository *plants = nullptr,
                   const ICatalogRepository *catalog = nullptr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Point the model at a plant (nullopt clears it), then recompute current values.
    void setPlant(std::optional<PlantId> plant);
    void refresh();

    // The plant's currently-bound sensors, for the multi-sensor / swap UI:
    // [{ sensorId, model, since, role }]. Empty when no plant or none bound.
    QVariantList boundSensors() const;
    bool hasPlant() const { return m_plant.has_value(); }

    // The history chart view-model for one quantity. loadHistory() fills it from EVERY
    // binding (open + closed) so the series follows the plant across sensor swaps.
    SeriesModel *history() { return &m_history; }
    void loadHistory(Quantity quantity);

    // Actions — all stamped from the injected clock so binding windows stay coherent.
    void attach(HandleKind kind, const QString &handleValue, const QString &model,
                std::span<const Reading> snapshot); // ensure sensor + bind + store snapshot
    void detach(SensorId sensor);                   // close the open binding for this plant

private:
    QDateTime nowUtc() const;
    QList<PlantSensorBinding> activeBindingsNow() const;
    CareStatus statusOf(const Reading &r) const; // evaluate against the cached ranges
    // The reading's 0..1 position in its ideal range for a metric bar (nullopt = no value
    // or no both-bounded range). Canonical-unit ratio — unit-independent.
    std::optional<double> fractionOf(const Reading &r) const;
    // The plant's effective care ranges: species catalog ideals overlaid with overrides.
    QList<CareRange> effectiveRangesForPlant() const;
    // This plant's readings for one quantity over [from, to] (for the extremes/DLI windows
    // the care judgment needs). Every binding, so the series follows the plant.
    QList<Reading> windowReadings(Quantity q, const QDateTime &from, const QDateTime &to) const;
    // Append a synthesized "Daily light" (Quantity::Dli) row to m_current — the
    // accumulated daily dose + its verdict — when a light sensor is bound and the plant has
    // a Dli range. The instantaneous light row stays for reference (value + bar, no pill).
    void appendDailyLightRow();

    ISensorRepository &m_sensors;
    IBindingRepository &m_bindings;
    IReadingRepository &m_readings;
    ICareThresholdRepository &m_thresholds;
    const IPlantRepository *m_plants = nullptr; // to read the plant's species (effective ranges)
    const ICatalogRepository *m_catalog = nullptr; // species ideals, overlaid by overrides
    const Clock &m_clock;
    const SettingsStore &m_settings;
    std::optional<PlantId> m_plant;
    std::optional<Quantity> m_historyQuantity; // last loadHistory() target, for re-conversion
    QList<Reading> m_current;    // one row per quantity, for the list model
    QList<CareRange> m_ranges;   // the plant's ideal ranges, cached per refresh()
    SeriesModel m_history;    // one quantity's history, filled on demand by loadHistory()
};

} // namespace klr
