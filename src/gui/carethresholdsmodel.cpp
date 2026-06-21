// SPDX-License-Identifier: GPL-3.0-or-later
#include "carethresholdsmodel.h"

#include "catalogthresholds.h"
#include "format.h"
#include "icarethresholdrepository.h"
#include "icatalogrepository.h"
#include "iplantrepository.h"
#include "log.h"
#include "plant.h"
#include "settingsstore.h"
#include "storageerror.h"
#include "units.h"

#include <span>

namespace klr {

namespace {

// A plain, locale-independent number for an editable field: "" when absent, else the
// value with trailing zeros trimmed (20.0 -> "20", 71.6 -> "71.6").
QString numText(std::optional<double> v)
{
    return v.has_value() ? QString::number(*v, 'g', 6) : QString();
}

// Parse an editable field back to a value; blank/garbage -> nullopt.
std::optional<double> parseNum(const QString &s)
{
    const QString t = s.trimmed();
    if (t.isEmpty())
        return std::nullopt;
    bool ok = false;
    const double v = t.toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

} // namespace

CareThresholdsModel::CareThresholdsModel(ICareThresholdRepository &thresholds,
                                         ICatalogRepository &catalog, IPlantRepository &plants,
                                         const SettingsStore &settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_thresholds(thresholds)
    , m_catalog(catalog)
    , m_plants(plants)
    , m_settings(settings)
{
    // Editing in display units: a unit-preference change re-renders the rows.
    connect(&m_settings, &SettingsStore::unitsChanged, this, [this] { refresh(); });
}

void CareThresholdsModel::setPlant(std::optional<PlantId> plant)
{
    m_plant = plant;
    refresh();
}

void CareThresholdsModel::refresh()
{
    beginResetModel();
    // Show the EFFECTIVE ranges (species catalog ideals overlaid with per-plant overrides),
    // so the editor starts from the species defaults without any seeding. Editing a field
    // writes an override; "Reset to species" clears the overrides back to the ideals.
    if (m_plant) {
        QString species;
        if (const std::optional<Plant> p = m_plants.get(*m_plant))
            species = p->species;
        m_ranges = effectiveRanges(&m_catalog, species, m_thresholds.thresholdsFor(*m_plant));
    } else {
        m_ranges = {};
    }
    endResetModel();
}

std::optional<CareRange> CareThresholdsModel::stored(Quantity q) const
{
    return rangeFor(std::span<const CareRange>(m_ranges.constData(), m_ranges.size()), q);
}

int CareThresholdsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(kQuantities.size());
}

QVariant CareThresholdsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= int(kQuantities.size()))
        return {};
    const Quantity q = kQuantities[index.row()];
    const Unit disp = displayUnit(q, m_settings.displayUnits());
    const std::optional<CareRange> r = stored(q);

    // Stored canonically; show in the display unit.
    auto toDisplay = [&](std::optional<double> v) -> std::optional<double> {
        return v ? std::optional<double>(convert(*v, canonicalUnit(q), disp)) : std::nullopt;
    };

    switch (role) {
    case QuantityRole: return int(q);
    case LabelRole:    return label(q);
    // DLI has no Unit enumerator (and no display-unit alternate); its symbol comes from
    // Format and toDisplay below is identity (canonical == display unit None).
    case UnitRole:     return q == Quantity::Dli ? dliUnitSymbol() : unitSymbol(disp);
    case MinTextRole:  return numText(r ? toDisplay(r->min) : std::nullopt);
    case MaxTextRole:  return numText(r ? toDisplay(r->max) : std::nullopt);
    case SetRole:      return r.has_value() && r->isSet();
    default:           return {};
    }
}

QHash<int, QByteArray> CareThresholdsModel::roleNames() const
{
    return {
        { QuantityRole, "quantity" }, { LabelRole, "label" },   { UnitRole, "unit" },
        { MinTextRole, "minText" },   { MaxTextRole, "maxText" }, { SetRole, "isSet" },
    };
}

void CareThresholdsModel::setRange(int quantity, const QString &minText, const QString &maxText)
{
    if (!m_plant || quantity < 0 || quantity >= kQuantityCount)
        return;
    const Quantity q = static_cast<Quantity>(quantity);
    const Unit disp = displayUnit(q, m_settings.displayUnits());

    // Parse the display-unit text and convert back to the canonical storage unit.
    auto toCanon = [&](const QString &s) -> std::optional<double> {
        const std::optional<double> v = parseNum(s);
        return v ? std::optional<double>(convert(*v, disp, canonicalUnit(q))) : std::nullopt;
    };

    const CareRange range{ q, toCanon(minText), toCanon(maxText) };
    // A threshold cannot be cleared away entirely: blanking BOTH bounds is rejected (it
    // would leave the quantity unjudged). Blanking ONE side is fine — it means "no limit
    // on that side". refresh() reverts the editor to the stored value.
    if (!range.isSet()) {
        refresh();
        return;
    }

    try {
        m_thresholds.setRange(*m_plant, range);
    } catch (const StorageError &e) {
        qCWarning(lcApp) << "set care threshold failed:" << e.what();
    }
    refresh();
    emit changed();
}

void CareThresholdsModel::resetToSpecies()
{
    if (!m_plant)
        return;
    const std::optional<Plant> p = m_plants.get(*m_plant);
    if (!p || p->species.isEmpty())
        return;
    // Data-driven model: ideals live in the catalog, the threshold table holds only
    // overrides. "Reset to species" therefore just CLEARS the overrides — refresh() then
    // shows the species' catalog ideals again. (No catalog lookup needed here.)
    try {
        m_thresholds.clear(*m_plant);
    } catch (const StorageError &ex) {
        qCWarning(lcApp) << "reset care thresholds failed:" << ex.what();
    }
    refresh();
    emit changed();
}

} // namespace klr
