// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QStringList>

// Backup restore (ADR 0010): the mirror image of BackupSerializer and a sibling of
// LegacyImporter. Parses a backup JSON and writes it back THROUGH the repositories (so
// the change-log + FK discipline hold), preserving every app-minted UUID so the dataset
// is reconstructed exactly — bindings/readings re-home to the same plants/sensors.
//
// Idempotent: every entity upserts by UUID (plants/journal get-or-update, sensors via the
// id-preserving add(), readings dedup per (sensor,quantity,bucket) in the repo, bindings
// dedup on (plant,sensor,validFrom)), so re-importing the same backup is a no-op.
//
// Forward-compatible: an unknown enum token warns and skips that row; a formatVersion
// newer than this build supports is refused with a clear StorageError.
namespace klr {

class IPlantRepository;
class ISensorRepository;
class IBindingRepository;
class IReadingRepository;
class IJournalRepository;
class IAttachmentRepository;
class ICareThresholdRepository;
class Clock;

class BackupImporter {
public:
    struct Result {
        int plants = 0;
        int sensors = 0;
        int bindings = 0;
        int readings = 0;
        int journal = 0;
        int attachments = 0;
        int thresholds = 0;
        QStringList warnings;
    };

    // The attachment repository is optional (nullptr in builds/tests without photo storage); when
    // absent the `attachments` array is ignored. Note: only the METADATA rows are restored — the
    // backup carries no file bytes (ADR 0024 decision 7), so restored photos resolve to a missing
    // file (UI shows "image unavailable") until the originals are present.
    BackupImporter(IPlantRepository &plants, ISensorRepository &sensors,
                   IBindingRepository &bindings, IReadingRepository &readings,
                   IJournalRepository &journal, ICareThresholdRepository &thresholds,
                   const Clock &clock, IAttachmentRepository *attachments = nullptr);

    // Parse and restore. Throws StorageError if the bytes are not a klorophylle backup or
    // were written by a newer formatVersion than this build supports.
    Result importFrom(const QByteArray &json);

private:
    IPlantRepository &m_plants;
    ISensorRepository &m_sensors;
    IBindingRepository &m_bindings;
    IReadingRepository &m_readings;
    IJournalRepository &m_journal;
    ICareThresholdRepository &m_thresholds;
    const Clock &m_clock;
    IAttachmentRepository *m_attachments = nullptr;
};

} // namespace klr
