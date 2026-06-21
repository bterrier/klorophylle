// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemoryattachmentrepository.h"

#include <algorithm>

namespace klr {

namespace {
// Oldest-first (added order); tie-break on id so ordering is deterministic.
void sortOldestFirst(QList<Attachment> &out)
{
    std::sort(out.begin(), out.end(), [](const Attachment &a, const Attachment &b) {
        if (a.addedAt != b.addedAt)
            return a.addedAt < b.addedAt;
        return a.id.toString() < b.id.toString();
    });
}
} // namespace

void InMemoryAttachmentRepository::add(const Attachment &attachment)
{
    m_byId.insert(attachment.id.value, attachment);
}

void InMemoryAttachmentRepository::updateCaption(AttachmentId id, const QString &caption)
{
    auto it = m_byId.find(id.value);
    if (it != m_byId.end())                  // unknown id is a no-op (mirrors SQLite UPDATE)
        it->caption = caption;
}

void InMemoryAttachmentRepository::remove(AttachmentId id)
{
    m_byId.remove(id.value);
}

QList<Attachment> InMemoryAttachmentRepository::forEntry(JournalEntryId entry) const
{
    QList<Attachment> out;
    for (const Attachment &a : m_byId) {
        if (a.entry == entry)
            out.append(a);
    }
    sortOldestFirst(out);
    return out;
}

QList<Attachment> InMemoryAttachmentRepository::all() const
{
    QList<Attachment> out;
    out.reserve(m_byId.size());
    for (const Attachment &a : m_byId)
        out.append(a);
    sortOldestFirst(out);
    return out;
}

} // namespace klr
