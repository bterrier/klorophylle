// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iattachmentfilestore.h"

#include <QtCore/QString>

// The real, disk-backed attachment file store (ADR 0024). Rooted at a base data directory (the same
// <AppData> that holds the DB); it owns the "attachments/" subdirectory beneath it. A fileRef is
// always "attachments/<uuid>.<ext>", relative to the base dir — so a copied/restored DB stays portable.
namespace klr {

class DiskAttachmentFileStore final : public IAttachmentFileStore {
public:
    // baseDir is the data directory (e.g. QStandardPaths AppDataLocation); the store manages
    // baseDir/attachments beneath it.
    explicit DiskAttachmentFileStore(QString baseDir) : m_baseDir(std::move(baseDir)) {}

    QString store(const QString &sourcePath, AttachmentId id) override;
    std::optional<QByteArray> read(const QString &fileRef) const override;
    void remove(const QString &fileRef) override;
    QString absolutePath(const QString &fileRef) const override;
    int sweepOrphans(const QSet<QString> &liveRefs) override;

private:
    QString m_baseDir;
};

} // namespace klr
