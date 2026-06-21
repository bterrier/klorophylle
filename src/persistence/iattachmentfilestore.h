// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QByteArray>
#include <QtCore/QSet>
#include <QtCore/QString>

#include <optional>

// The bytes behind a journal attachment (ADR 0024 decision 4). Kept SEPARATE from
// IAttachmentRepository: a filesystem copy cannot join a DB transaction, and keeping them apart lets
// the in-memory repository fake stay disk-free. DiskAttachmentFileStore is the real impl;
// InMemoryAttachmentFileStore is the RAM fake for higher-layer tests.
namespace klr {

class IAttachmentFileStore {
public:
    virtual ~IAttachmentFileStore() = default;

    // Copy sourcePath into the store under a name derived from id + the source's extension; return
    // the app-data-relative fileRef ("attachments/<uuid>.<ext>") to persist on the Attachment row.
    // Throws StorageError if the source can't be read or the copy fails.
    virtual QString store(const QString &sourcePath, AttachmentId id) = 0;

    // Load the bytes for a fileRef, or nullopt if the file is absent — e.g. a backup restored on a
    // machine without the original files (ADR 0024 decision 7). nullopt is "no file", not an error.
    virtual std::optional<QByteArray> read(const QString &fileRef) const = 0;

    // Delete the backing file. A missing file is a no-op (idempotent) — the safe direction when the
    // row was already removed.
    virtual void remove(const QString &fileRef) = 0;

    // Absolute path for the fileRef (for QML Image { source: ... }). Does not check existence.
    virtual QString absolutePath(const QString &fileRef) const = 0;

    // Mark-and-sweep orphan cleanup (ADR 0024 decision 5): delete every stored file whose fileRef is
    // NOT in liveRefs. Returns the number of files removed. Never deletes a row (it has none) and is
    // safe to run at startup — a file-without-row is just wasted disk; a row-without-file is tolerated.
    virtual int sweepOrphans(const QSet<QString> &liveRefs) = 0;
};

} // namespace klr
