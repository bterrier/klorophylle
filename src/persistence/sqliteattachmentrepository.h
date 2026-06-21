// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "iattachmentrepository.h"

// The SQLite-backed attachment repository. The ONLY attachment code that touches SQL.
// Metadata only — the file bytes live behind IAttachmentFileStore (ADR 0024).
namespace klr {

class SqliteAttachmentRepository final : public IAttachmentRepository {
public:
    explicit SqliteAttachmentRepository(Database &db) : m_db(db) {}

    void add(const Attachment &attachment) override;
    void updateCaption(AttachmentId id, const QString &caption) override;
    void remove(AttachmentId id) override;
    QList<Attachment> forEntry(JournalEntryId entry) const override; // oldest-first
    QList<Attachment> all() const override;

private:
    Database &m_db;
};

} // namespace klr
