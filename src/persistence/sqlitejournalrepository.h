// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "ijournalrepository.h"

// The SQLite-backed journal repository. The ONLY journal code that touches SQL.
namespace klr {

class SqliteJournalRepository final : public IJournalRepository {
public:
    explicit SqliteJournalRepository(Database &db) : m_db(db) {}

    void add(const JournalEntry &entry) override;
    void update(const JournalEntry &entry) override;
    void remove(JournalEntryId id) override;
    QList<JournalEntry> forPlant(PlantId plant) const override; // newest-first
    QList<JournalEntry> globalEntries() const override;         // plant-less, newest-first

private:
    Database &m_db;
};

} // namespace klr
