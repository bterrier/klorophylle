// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QString>

// Clones an existing plant into a brand-new one. The motivating case: a user tracked two
// real plants sharing one pot + one sensor as a single plant, then repotted them into two
// pots (usually adding a sensor). Duplicating gives the second pot its own plant while it
// keeps the FULL shared history.
//
// History follows the plant THROUGH its bindings (readings key on the sensor, not the
// plant — ADR 0005/0006), so "full history in each" is achieved by copying the source
// plant's binding history VERBATIM (original validFrom/validTo/role), not by copying any
// readings. The duplicate then resolves the very same historical samples. Reassigning a
// sensor going forward (the repot) is left to the caller via the normal attach/detach
// path — "history follows the plant" then handles the split automatically.
//
// Pure orchestration over the repository interfaces (no SQL of its own — respects the
// repository boundary), so it is unit-tested against the in-memory fakes.
namespace klr {

class IPlantRepository;
class IJournalRepository;
class ICareThresholdRepository;
class IBindingRepository;

class PlantDuplicator {
public:
    PlantDuplicator(IPlantRepository &plants, IJournalRepository &journal,
                    ICareThresholdRepository &thresholds, IBindingRepository &bindings);

    // Clone `source` into a new plant with a fresh PlantId, `newName` as display name and
    // the source's species + trackedSince. Copies the care thresholds, every journal entry
    // (fresh ids, same content/timestamps) and the full binding history. Returns the new id.
    // Throws StorageError (from a repository) if the source is missing or a write fails;
    // since there is no cross-repository transaction yet, a failure mid-way can leave a
    // partial duplicate — visible and deletable by the user (offline single-user product).
    PlantId duplicate(PlantId source, const QString &newName);

private:
    IPlantRepository &m_plants;
    IJournalRepository &m_journal;
    ICareThresholdRepository &m_thresholds;
    IBindingRepository &m_bindings;
};

} // namespace klr
