// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "carestatus.h" // CareLevel
#include "plant.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>

namespace klr {

class IPlantRepository;
class ISensorRepository;
class IBindingRepository;
class IReadingRepository;
class ICareThresholdRepository;
class ICatalogRepository;
class SettingsStore;
class Clock;
class BleScanner;

// Adapts the plant repository to QML. Thin: it reads through the repository and
// resets on demand (the plant set changes rarely). QML binds to this; it never
// sees the repository or SQL.
//
// At-a-glance plant health: given the (optional) sensor/binding/reading/threshold
// repos + clock, each row also carries a CareLevel rollup — the worst-of its current
// readings vs the plant's ideal ranges. Without those (e.g. a plant-only unit test) the
// level is Unknown and the list still works.
class PlantListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        PlantIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        SpeciesRole,
        TrackedSinceRole, // localized short date
        HealthRole,       // CareLevel (int): Good / Attention / Unknown
        MoistureRole,     // QVariantMap { present, valueText, fraction, hasRange }
        LightRole,        // QVariantMap { present, valueText, fraction, hasRange }
        ConnectivityRole, // Liveness (int): Offline/Stale/Live; -1 when no sensor bound
    };

    explicit PlantListModel(IPlantRepository &repo, QObject *parent = nullptr);
    // With health context: the rollup is computed against live readings + thresholds.
    // The (optional) SettingsStore makes the moisture/light value text display-unit aware
    // (e.g. lux↔µmol); without it the canonical formatting is used.
    PlantListModel(IPlantRepository &repo, ISensorRepository *sensors, IBindingRepository *bindings,
                   IReadingRepository *readings, ICareThresholdRepository *thresholds,
                   const ICatalogRepository *catalog, const Clock *clock,
                   const SettingsStore *settings = nullptr, BleScanner *scanner = nullptr,
                   QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh(); // reload from the repository (and recompute health + connectivity)

    // Recompute ONLY each plant's connectivity (Liveness) from the scanner's per-handle
    // last-seen / latest-value freshness + the clock, and emit dataChanged(ConnectivityRole)
    // for the rows whose value actually changed. Cheap (no SQL, no reset) — driven on a timer
    // from the composition root so green→red transitions happen even with no new broadcast.
    Q_INVOKABLE void refreshConnectivity();

private:
    // One presentational metric for the plant card: the current moisture/light
    // reading rendered for the home list. `fraction` (0..1) is the value's position in
    // the plant's ideal range and is only meaningful when `hasRange`; with a reading but
    // no ideal range the card shows `valueText` as a chip (no bar). No sentinel: absence
    // is the explicit `present` / `hasRange` bools (never a magic value).
    struct Metric {
        bool present = false;
        QString valueText;
        double fraction = 0.0;
        bool hasRange = false;
    };
    // Per-row care snapshot, computed once in refresh() from the same reading fetch that
    // drives the health rollup (no extra repository round-trips).
    struct RowCare {
        CareLevel level = CareLevel::Unknown;
        Metric moisture;
        Metric light;
        // Connectivity cache: the plant's bound-sensor handles (resolved once in refresh()),
        // and the last-emitted Liveness as an int (-1 == no sensor bound, so no dot). The
        // timer-driven refreshConnectivity() recomputes only the int from the handles.
        QStringList handles;
        int connectivity = -1;
    };
    RowCare careOf(const Plant &p) const;
    // The plant's connectivity (Liveness int, or -1 for no sensors) from its cached handles,
    // judged via the scanner's freshness + the injected clock.
    int connectivityOf(const QStringList &handles) const;
    // Render one current reading (or none) as a card metric against the plant's ranges.
    Metric metricOf(const Reading *r, std::span<const CareRange> ranges) const;
    static QVariantMap metricMap(const Metric &m); // -> QML { present, valueText, fraction, hasRange }

    IPlantRepository &m_repo;
    ISensorRepository *m_sensors = nullptr;
    IBindingRepository *m_bindings = nullptr;
    IReadingRepository *m_readings = nullptr;
    ICareThresholdRepository *m_thresholds = nullptr;
    const ICatalogRepository *m_catalog = nullptr;
    const SettingsStore *m_settings = nullptr;
    const Clock *m_clock = nullptr;
    BleScanner *m_scanner = nullptr; // optional; connectivity needs it (per-handle freshness)
    QList<Plant> m_rows;
    QList<RowCare> m_care; // parallel to m_rows, computed in refresh()
};

} // namespace klr
