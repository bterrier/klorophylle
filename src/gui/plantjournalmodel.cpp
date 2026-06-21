// SPDX-License-Identifier: GPL-3.0-or-later
#include "plantjournalmodel.h"

#include "iattachmentfilestore.h"
#include "iattachmentrepository.h"
#include "ijournalrepository.h"
#include "journalformat.h"

#include <QtCore/QLocale>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>

namespace klr {

PlantJournalModel::PlantJournalModel(IJournalRepository &repo, IAttachmentRepository *attachments,
                                     IAttachmentFileStore *fileStore, QObject *parent)
    : QAbstractListModel(parent)
    , m_repo(repo)
    , m_attachments(attachments)
    , m_fileStore(fileStore)
{
}

void PlantJournalModel::setPlant(PlantId plant)
{
    m_plant = plant;
    m_hasPlant = true;
    m_global = false;
    refresh();
}

void PlantJournalModel::clearPlant()
{
    m_hasPlant = false;
    refresh();
}

void PlantJournalModel::setGlobal()
{
    m_global = true;
    m_hasPlant = false;
    refresh();
}

void PlantJournalModel::refresh()
{
    beginResetModel();
    m_rows = m_global         ? m_repo.globalEntries()
        : m_hasPlant          ? m_repo.forPlant(m_plant)
                              : QList<JournalEntry>{};
    endResetModel();
}

int PlantJournalModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PlantJournalModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const JournalEntry &e = m_rows.at(index.row());
    switch (role) {
    case EntryIdRole:   return e.id.toString();
    case KindRole:      return int(e.kind);
    case KindLabelRole: return journalKindLabel(e.kind);
    case TimestampRole: return QLocale().toString(e.timestamp.toLocalTime(), QLocale::ShortFormat);
    case EditedAtRole:  return e.editedAt // empty string == never edited (no edit line in the UI)
            ? QLocale().toString(e.editedAt->toLocalTime(), QLocale::ShortFormat)
            : QString();
    case NoteRole:      return e.note;
    case NoteSummaryRole: return journalNoteSummary(e.note);
    case IsMemoryRole:  return e.kind == JournalEntryKind::Memory;
    case AttachmentsRole: return attachmentsFor(e.id);
    default:            return {};
    }
}

QVariantList PlantJournalModel::attachmentsFor(JournalEntryId entry) const
{
    QVariantList out;
    if (!m_attachments || !m_fileStore)
        return out; // no attachment storage wired (e.g. the global journal)
    for (const Attachment &a : m_attachments->forEntry(entry)) {
        out.append(QVariantMap{
            { QStringLiteral("attachmentId"), a.id.toString() },
            // A local file:// URL for QML Image { source }. The file may be absent (restored backup
            // without files) — QML renders nothing / a placeholder, never crashes.
            { QStringLiteral("url"), QUrl::fromLocalFile(m_fileStore->absolutePath(a.fileRef)).toString() },
            { QStringLiteral("caption"), a.caption },
        });
    }
    return out;
}

QHash<int, QByteArray> PlantJournalModel::roleNames() const
{
    return {
        { EntryIdRole, "entryId" },
        { KindRole, "kind" },
        { KindLabelRole, "kindLabel" },
        { TimestampRole, "timestamp" },
        { EditedAtRole, "editedAt" },
        { NoteRole, "note" },
        { NoteSummaryRole, "noteSummary" },
        { IsMemoryRole, "isMemory" },
        { AttachmentsRole, "attachments" },
    };
}

} // namespace klr
