// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"
#include "journalentry.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QtCore/QVariantList>

namespace klr {

class IJournalRepository;
class IAttachmentRepository;
class IAttachmentFileStore;

// A journal view, newest-first: either the selected plant's entries (setPlant) or the GLOBAL,
// plant-less journal (setGlobal, ADR 0022). Thin per-screen model: kind labels and
// timestamps are formatted in C++ and surfaced as read-only roles.
class PlantJournalModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        EntryIdRole = Qt::UserRole + 1,
        KindRole,      // int (JournalEntryKind)
        KindLabelRole, // localized
        TimestampRole, // localized short date-time (the entry date)
        EditedAtRole,  // localized short date-time when edited, else empty (never-edited)
        NoteRole,
        NoteSummaryRole, // first non-blank line of the note — git-commit-style list subject
        IsMemoryRole,  // bool: the agent-authored Memory kind — drawn with the cyan AI accent
        AttachmentsRole, // QVariantList of {attachmentId, url, caption} for the entry's photos
    };

    // The attachment repo + file store are optional: when both are injected, AttachmentsRole resolves
    // each entry's photos (metadata from the repo, a file:// URL from the store); without them it is
    // always empty (e.g. the global journal, or a build without attachment storage).
    explicit PlantJournalModel(IJournalRepository &repo, IAttachmentRepository *attachments = nullptr,
                               IAttachmentFileStore *fileStore = nullptr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlant(PlantId plant);
    void clearPlant();
    // Scope this model to the global (plant-less) journal — the agent's user-wide memory + global
    // notes (ADR 0022). Mutually exclusive with setPlant.
    void setGlobal();
    void refresh();

private:
    // The entry's photos as {attachmentId, url, caption} maps (empty without attachment storage).
    QVariantList attachmentsFor(JournalEntryId entry) const;

    IJournalRepository &m_repo;
    IAttachmentRepository *m_attachments = nullptr;
    IAttachmentFileStore *m_fileStore = nullptr;
    PlantId m_plant;
    bool m_hasPlant = false;
    bool m_global = false;
    QList<JournalEntry> m_rows;
};

} // namespace klr
