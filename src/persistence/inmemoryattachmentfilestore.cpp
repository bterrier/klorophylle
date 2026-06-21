// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemoryattachmentfilestore.h"
#include "storageerror.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace klr {

QString InMemoryAttachmentFileStore::store(const QString &sourcePath, AttachmentId id)
{
    QFile f(sourcePath);
    if (!f.open(QIODevice::ReadOnly))
        throw StorageError(QStringLiteral("attachment store (fake): cannot read %1").arg(sourcePath));
    QString ext = QFileInfo(sourcePath).suffix().toLower();
    if (ext.isEmpty())
        ext = QStringLiteral("jpg");
    const QString ref = QStringLiteral("attachments/%1.%2").arg(id.toString(), ext);
    m_files.insert(ref, f.readAll());
    return ref;
}

std::optional<QByteArray> InMemoryAttachmentFileStore::read(const QString &fileRef) const
{
    const auto it = m_files.constFind(fileRef);
    if (it == m_files.constEnd())
        return std::nullopt;
    return *it;
}

void InMemoryAttachmentFileStore::remove(const QString &fileRef)
{
    m_files.remove(fileRef);
}

QString InMemoryAttachmentFileStore::absolutePath(const QString &fileRef) const
{
    return QStringLiteral("memory:/%1").arg(fileRef); // synthetic — the fake has no real files
}

int InMemoryAttachmentFileStore::sweepOrphans(const QSet<QString> &liveRefs)
{
    int removed = 0;
    for (const QString &ref : m_files.keys()) {
        if (!liveRefs.contains(ref)) {
            m_files.remove(ref);
            ++removed;
        }
    }
    return removed;
}

} // namespace klr
