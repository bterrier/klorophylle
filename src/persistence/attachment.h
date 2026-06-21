// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QDateTime>
#include <QtCore/QString>

// A single attachment on a journal entry — a photo, for now (ADR 0024). Photos are an
// orthogonal evidence axis on ANY entry, never a JournalEntryKind: one entry owns
// zero-or-many attachments (e.g. before/after shots on a Repotting). A plain value type,
// like JournalEntry/Plant.
namespace klr {

struct Attachment {
    AttachmentId id;
    // The journal entry this attachment belongs to — the stable anchor it cascade-deletes with.
    JournalEntryId entry;
    // App-data-relative path to the file ("attachments/<uuid>.<ext>"), never a BLOB and never
    // absolute, so a copied/restored DB stays portable (ADR 0024 decision 2). The IAttachmentFileStore
    // joins this to its root for the bytes.
    QString fileRef;
    // Free text disambiguating multiple shots on one entry — "Before" / "After" a repot, a leaf vs.
    // the whole plant. Optional; empty by default.
    QString caption;
    // When the attachment was added (UTC). Stamped from the injected Clock at the business edge
    // (AttachmentController), never a wall-clock read in the repository.
    QDateTime addedAt;

    bool operator==(const Attachment &) const = default;
};

} // namespace klr
