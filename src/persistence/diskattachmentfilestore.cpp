// SPDX-License-Identifier: GPL-3.0-or-later
#include "diskattachmentfilestore.h"
#include "storageerror.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace klr {

namespace {
constexpr QLatin1StringView kSubdir{ "attachments" };
constexpr QLatin1StringView kDefaultExt{ "jpg" };

// "attachments/<uuid>.<ext>" — the app-data-relative ref stored on the row.
QString refFor(AttachmentId id, const QString &ext)
{
    return QStringLiteral("%1/%2.%3").arg(QString(kSubdir), id.toString(), ext);
}
} // namespace

QString DiskAttachmentFileStore::store(const QString &sourcePath, AttachmentId id)
{
    QFileInfo src(sourcePath);
    QString ext = src.suffix().toLower();
    if (ext.isEmpty())
        ext = QString(kDefaultExt);

    const QDir dir(m_baseDir);
    if (!dir.mkpath(QString(kSubdir)))
        throw StorageError(QStringLiteral("attachment store: cannot create %1/%2")
                               .arg(m_baseDir, QString(kSubdir)));

    const QString ref = refFor(id, ext);
    const QString dest = dir.filePath(ref);
    QFile::remove(dest); // overwrite any stale file at this id (idempotent re-store)
    if (!QFile::copy(sourcePath, dest))
        throw StorageError(QStringLiteral("attachment store: cannot copy %1 to %2")
                               .arg(sourcePath, dest));
    return ref;
}

std::optional<QByteArray> DiskAttachmentFileStore::read(const QString &fileRef) const
{
    QFile f(absolutePath(fileRef));
    if (!f.exists())
        return std::nullopt;                 // absent file (e.g. restored backup) — not an error
    if (!f.open(QIODevice::ReadOnly))
        throw StorageError(QStringLiteral("attachment read: cannot open %1").arg(f.fileName()));
    return f.readAll();
}

void DiskAttachmentFileStore::remove(const QString &fileRef)
{
    QFile::remove(absolutePath(fileRef)); // missing file is a no-op
}

QString DiskAttachmentFileStore::absolutePath(const QString &fileRef) const
{
    return QDir(m_baseDir).filePath(fileRef);
}

int DiskAttachmentFileStore::sweepOrphans(const QSet<QString> &liveRefs)
{
    const QDir dir(QDir(m_baseDir).filePath(QString(kSubdir)));
    if (!dir.exists())
        return 0;
    int removed = 0;
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : files) {
        const QString ref = QStringLiteral("%1/%2").arg(QString(kSubdir), fi.fileName());
        if (!liveRefs.contains(ref)) {
            if (QFile::remove(fi.absoluteFilePath()))
                ++removed;
        }
    }
    return removed;
}

} // namespace klr
