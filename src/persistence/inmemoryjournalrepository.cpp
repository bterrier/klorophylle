// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemoryjournalrepository.h"

#include <algorithm>

namespace klr {

namespace {
// Newest-first; tie-break on id so ordering is deterministic.
void sortNewestFirst(QList<JournalEntry> &out)
{
    std::sort(out.begin(), out.end(), [](const JournalEntry &a, const JournalEntry &b) {
        if (a.timestamp != b.timestamp)
            return a.timestamp > b.timestamp;
        return a.id.toString() > b.id.toString();
    });
}
} // namespace

void InMemoryJournalRepository::add(const JournalEntry &entry)
{
    m_byId.insert(entry.id.value, entry);
}

void InMemoryJournalRepository::update(const JournalEntry &entry)
{
    m_byId.insert(entry.id.value, entry);
}

void InMemoryJournalRepository::remove(JournalEntryId id)
{
    m_byId.remove(id.value);
}

QList<JournalEntry> InMemoryJournalRepository::forPlant(PlantId plant) const
{
    QList<JournalEntry> out;
    for (const JournalEntry &e : m_byId) {
        if (e.plant == plant)                   // optional<PlantId> == PlantId: false when global
            out.append(e);
    }
    sortNewestFirst(out);
    return out;
}

QList<JournalEntry> InMemoryJournalRepository::globalEntries() const
{
    QList<JournalEntry> out;
    for (const JournalEntry &e : m_byId) {
        if (!e.plant)                           // nullopt == a global (plant-less) entry
            out.append(e);
    }
    sortNewestFirst(out);
    return out;
}

} // namespace klr
