// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"
#include "journalentry.h"

#include <QtCore/QList>

// The repository boundary for the plant care journal (see iplantrepository.h).
// SqliteJournalRepository implements this; tests run the same behavioural suite
// against InMemoryJournalRepository and SqliteJournalRepository.
//
// Attachments are a SEPARATE repository (built — ADR 0024): a journal entry is the stable anchor for
// zero-or-many photos, keyed by JournalEntryId in `attachments(file_ref, caption, added_at)`,
// cascade-deleted with the entry. A photo is NOT a JournalEntryKind — it is an orthogonal evidence
// axis on any entry. See IAttachmentRepository + IAttachmentFileStore (file bytes live on disk, only
// metadata in SQL).
namespace klr {

class IJournalRepository {
public:
    virtual ~IJournalRepository() = default;

    virtual void add(const JournalEntry &entry) = 0;
    virtual void update(const JournalEntry &entry) = 0;
    virtual void remove(JournalEntryId id) = 0;
    virtual QList<JournalEntry> forPlant(PlantId plant) const = 0; // newest-first
    // The GLOBAL journal: plant-less entries (plant == nullopt), newest-first. Holds the agent's
    // user-wide Memory entry + user global notes (ADR 0022).
    virtual QList<JournalEntry> globalEntries() const = 0;
};

} // namespace klr
