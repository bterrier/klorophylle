// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ijournalrepository.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QUuid>

namespace klr {

// The test/fake journal repository.
class InMemoryJournalRepository final : public IJournalRepository {
public:
    void add(const JournalEntry &entry) override;
    void update(const JournalEntry &entry) override;
    void remove(JournalEntryId id) override;
    QList<JournalEntry> forPlant(PlantId plant) const override; // newest-first
    QList<JournalEntry> globalEntries() const override;         // plant-less, newest-first

private:
    QHash<QUuid, JournalEntry> m_byId; // keyed by JournalEntryId::value
};

} // namespace klr
