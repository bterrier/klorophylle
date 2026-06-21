// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iattachmentfilestore.h"

#include <QtCore/QHash>

// The RAM fake attachment file store — for higher-layer tests (controller, agent tool) that need a
// file store without touching the disk. Mirrors DiskAttachmentFileStore's fileRef shape
// ("attachments/<uuid>.<ext>") so callers behave identically.
namespace klr {

class InMemoryAttachmentFileStore final : public IAttachmentFileStore {
public:
    QString store(const QString &sourcePath, AttachmentId id) override;
    std::optional<QByteArray> read(const QString &fileRef) const override;
    void remove(const QString &fileRef) override;
    QString absolutePath(const QString &fileRef) const override;
    int sweepOrphans(const QSet<QString> &liveRefs) override;

    // Test helper: register bytes for a ref directly (no source file needed).
    void put(const QString &fileRef, const QByteArray &bytes) { m_files.insert(fileRef, bytes); }

private:
    QHash<QString, QByteArray> m_files; // fileRef -> bytes
};

} // namespace klr
