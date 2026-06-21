// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iattachmentrepository.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QUuid>

namespace klr {

// The test/fake attachment repository — metadata only, never touches the disk.
class InMemoryAttachmentRepository final : public IAttachmentRepository {
public:
    void add(const Attachment &attachment) override;
    void updateCaption(AttachmentId id, const QString &caption) override;
    void remove(AttachmentId id) override;
    QList<Attachment> forEntry(JournalEntryId entry) const override; // oldest-first
    QList<Attachment> all() const override;

private:
    QHash<QUuid, Attachment> m_byId; // keyed by AttachmentId::value
};

} // namespace klr
