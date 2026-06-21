// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "carestatus.h" // klr_core: CareRange
#include "catalogentry.h"

#include <QtCore/QList>

// Pure mapping from a catalog species' ideal-range fields to the per-quantity CareRanges
// the care-status judgment uses. This is the SEED
// for a plant's thresholds: the catalog is the single owner of *ideal* ranges
// (immutable reference data), and a per-plant copy in care_thresholds is the mutable,
// overridable "active" set.
//
// Quantities mapped: soil moisture (%), soil conductivity (µS/cm), air temperature (°C,
// the unit Flower Care broadcasts), air humidity (%RH), illuminance (lux) and
// the daily-light-integral column as Quantity::Dli (mmol·m⁻²·day⁻¹), which is how light
// is actually judged (the lux range survives only as a display range for the metric bar).
// The catalog's pH has no Quantity and is still NOT mapped. A field pair contributes a
// range only when at least one bound is present.
namespace klr {

class ICatalogRepository;

QList<CareRange> idealRanges(const CatalogEntry &entry);

// Overlay per-plant overrides onto species ideals: the result has one range per quantity
// present in either, with an override winning over the ideal for the same quantity. Pure.
// This is the data-driven heart of care judgment — the catalog stays the single owner of
// ideals (immutable), the override table holds ONLY user edits, and nothing is ever
// materialised/seeded by a UI action (ADR 0003).
QList<CareRange> mergeRanges(const QList<CareRange> &ideals, const QList<CareRange> &overrides);

// A plant's EFFECTIVE care ranges: its species' catalog ideals overlaid with `overrides`.
// `catalog` may be null and `species` empty (then only the overrides apply). Convenience
// over byKey + idealRanges + mergeRanges for the model consumers.
QList<CareRange> effectiveRanges(const ICatalogRepository *catalog, const QString &species,
                                 const QList<CareRange> &overrides);

} // namespace klr
