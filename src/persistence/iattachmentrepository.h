// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "attachment.h"
#include "ids.h"

#include <QtCore/QList>

// The repository boundary for journal attachments (photos, for now — ADR 0024). METADATA ONLY:
// the file bytes live behind IAttachmentFileStore, never here (a filesystem copy can't join a DB
// transaction, and keeping them apart lets the in-memory fake stay disk-free). SqliteAttachmentRepository
// implements this; the same behavioural suite runs against InMemoryAttachmentRepository and the SQLite
// impl. The journal entry is the stable anchor — attachments cascade-delete with it (FK in schema.cpp).
namespace klr {

class IAttachmentRepository {
public:
    virtual ~IAttachmentRepository() = default;

    virtual void add(const Attachment &attachment) = 0;
    virtual void updateCaption(AttachmentId id, const QString &caption) = 0;
    virtual void remove(AttachmentId id) = 0;
    virtual QList<Attachment> forEntry(JournalEntryId entry) const = 0; // oldest-first (added order)
    virtual QList<Attachment> all() const = 0;                          // every row (backup, sweep)
};

} // namespace klr
