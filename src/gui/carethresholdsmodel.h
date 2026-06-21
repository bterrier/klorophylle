// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "carestatus.h" // CareRange
#include "ids.h"
#include "reading.h" // Quantity

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>

#include <array>
#include <optional>

namespace klr {

class ICareThresholdRepository;
class ICatalogRepository;
class IPlantRepository;
class SettingsStore;

// The editable per-plant care-threshold list: one row per "carable" quantity
// (soil moisture, soil conductivity, air temperature, air humidity, light), each with
// an editable min/max. Thresholds are STORED canonically; this model shows and accepts
// them in the user's display unit and converts at the boundary — so a user on °F
// edits °F. resetToSpecies() re-seeds from the plant's catalog species. Thin: parsing
// + conversion are the only logic, all pure/tested below. No SQL — the repositories are
// injected (no setContextProperty).
class CareThresholdsModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        QuantityRole = Qt::UserRole + 1,
        LabelRole,    // "Soil moisture"
        UnitRole,     // display-unit symbol ("%", "°F", …)
        MinTextRole,  // editable number in the display unit ("" when unset)
        MaxTextRole,
        SetRole,      // bool: a bound is set for this quantity
    };

    // The quantities a plant's care is judged on (the ones the catalog seeds). Light is the
    // Daily Light Integral dose (Quantity::Dli, mmol·m⁻²·day⁻¹), not the instantaneous lux —
    // that is how light is judged; the lux range is display-only and not edited here.
    static constexpr std::array<Quantity, 5> kQuantities{
        Quantity::SoilMoisture, Quantity::SoilConductivity, Quantity::AirTemperature,
        Quantity::AirHumidity, Quantity::Dli
    };

    CareThresholdsModel(ICareThresholdRepository &thresholds, ICatalogRepository &catalog,
                        IPlantRepository &plants, const SettingsStore &settings,
                        QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlant(std::optional<PlantId> plant);
    void refresh();

    // Store one quantity's range, parsed from display-unit text (empty == that bound is
    // cleared; both empty removes the threshold). Re-reads + emits.
    Q_INVOKABLE void setRange(int quantity, const QString &minText, const QString &maxText);
    // Replace all thresholds with the plant's catalog-species ideal ranges (the explicit
    // ideal->active sync). No-op without a species / catalog entry.
    Q_INVOKABLE void resetToSpecies();

signals:
    void changed(); // a threshold was edited (so the care status can refresh)

private:
    std::optional<CareRange> stored(Quantity q) const; // canonical range for a quantity

    ICareThresholdRepository &m_thresholds;
    ICatalogRepository &m_catalog;
    IPlantRepository &m_plants;
    const SettingsStore &m_settings;
    std::optional<PlantId> m_plant;
    QList<CareRange> m_ranges; // canonical, cached per refresh()
};

} // namespace klr
