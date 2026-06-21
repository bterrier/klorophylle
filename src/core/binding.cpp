// SPDX-License-Identifier: GPL-3.0-or-later
#include "binding.h"

namespace klr {

bool isActiveAt(const PlantSensorBinding &b, const QDateTime &at)
{
    if (at < b.validFrom)
        return false;
    // Half-open at the top: a sample exactly at validTo belongs to the next binding.
    return !b.validTo.has_value() || at < *b.validTo;
}

QList<PlantSensorBinding> activeBindings(std::span<const PlantSensorBinding> all,
                                         const QDateTime &at)
{
    QList<PlantSensorBinding> out;
    for (const PlantSensorBinding &b : all) {
        if (isActiveAt(b, at))
            out.append(b);
    }
    return out;
}

namespace {

// Half-open [validFrom, validTo) intervals; nullopt validTo == +infinity. Two
// intervals overlap iff each starts before the other ends.
bool windowsOverlap(const PlantSensorBinding &a, const PlantSensorBinding &b)
{
    const bool aBeforeBEnd = !b.validTo.has_value() || a.validFrom < *b.validTo;
    const bool bBeforeAEnd = !a.validTo.has_value() || b.validFrom < *a.validTo;
    return aBeforeBEnd && bBeforeAEnd;
}

} // namespace

std::expected<void, BindingError> validateBinding(
    std::span<const PlantSensorBinding> existingForPlant,
    const PlantSensorBinding &candidate)
{
    if (!candidate.role.has_value())
        return {}; // a no-role binding never conflicts (redundant probes are allowed)

    for (const PlantSensorBinding &e : existingForPlant) {
        // Only explicit-role-vs-explicit-role on the same quantity can conflict.
        if (e.role == candidate.role && windowsOverlap(e, candidate))
            return std::unexpected(BindingError::RoleConflict);
    }
    return {};
}

} // namespace klr
