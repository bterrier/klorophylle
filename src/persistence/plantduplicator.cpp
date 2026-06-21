// SPDX-License-Identifier: GPL-3.0-or-later
#include "plantduplicator.h"

#include "carestatus.h" // CareRange
#include "ibindingrepository.h"
#include "icarethresholdrepository.h"
#include "ijournalrepository.h"
#include "iplantrepository.h"
#include "journalentry.h"
#include "plant.h"
#include "storageerror.h"

#include <span>

namespace klr {

PlantDuplicator::PlantDuplicator(IPlantRepository &plants, IJournalRepository &journal,
                                 ICareThresholdRepository &thresholds, IBindingRepository &bindings)
    : m_plants(plants), m_journal(journal), m_thresholds(thresholds), m_bindings(bindings)
{
}

PlantId PlantDuplicator::duplicate(PlantId source, const QString &newName)
{
    const std::optional<Plant> src = m_plants.get(source);
    if (!src)
        throw StorageError(QStringLiteral("duplicate: source plant not found"));

    // 1. The plant row — new identity, user-chosen name, same species + start date so the
    //    duplicate's age/timeline matches the original it shared a pot with.
    Plant copy;
    copy.id = PlantId::generate();
    copy.displayName = newName.trimmed();
    copy.species = src->species;
    copy.trackedSince = src->trackedSince;
    m_plants.add(copy);

    // Each repository commits its own transaction, so the clone is not one atomic unit.
    // Once the plant row exists, compensate on any later failure: delete the partial copy
    // (its ON DELETE CASCADE clears any thresholds/journal/bindings already written), then
    // rethrow — so a failed duplicate never leaves a stray half-made plant behind.
    try {
        // 2. Care thresholds — the deliberate per-plant overrides carry over (same species).
        const QList<CareRange> ranges = m_thresholds.thresholdsFor(source);
        m_thresholds.replaceAll(copy.id,
                                std::span<const CareRange>(ranges.constData(), ranges.size()));

        // 3. Journal — the care events happened to the shared pot, so each plant keeps the
        //    full record. Fresh entry ids; same timestamp/kind/note/editedAt.
        const QList<JournalEntry> entries = m_journal.forPlant(source);
        for (const JournalEntry &e : entries) {
            JournalEntry j;
            j.id = JournalEntryId::generate();
            j.plant = copy.id;
            j.timestamp = e.timestamp;
            j.kind = e.kind;
            j.note = e.note;
            j.editedAt = e.editedAt; // preserve the edit history verbatim on the clone
            m_journal.add(j);
        }

        // 4. Binding history — VERBATIM so the duplicate resolves the same readings (history
        //    follows the plant through its binding windows). bindings() is ordered by
        //    valid_from; replaying bind (+ unbind for a closed edge) in that order preserves
        //    the windows and the per-plant non-overlap invariant. Open edges stay open, so
        //    the duplicate shares the currently-bound sensor until the caller reassigns it.
        const QList<PlantSensorBinding> hist = m_bindings.bindings(source);
        for (const PlantSensorBinding &b : hist) {
            m_bindings.bind(copy.id, b.sensor, b.validFrom, b.role);
            if (b.validTo)
                m_bindings.unbind(copy.id, b.sensor, *b.validTo);
        }
    } catch (...) {
        try {
            m_plants.remove(copy.id); // cascade-cleans any children written before the failure
        } catch (...) {
            // The cleanup itself failed (e.g. the same I/O fault) — nothing more we can do;
            // surface the ORIGINAL failure below rather than masking it with this one.
        }
        throw;
    }

    return copy.id;
}

} // namespace klr
