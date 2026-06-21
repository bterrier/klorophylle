// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>

// Backup serialization (ADR 0010): a lossless, id-preserving JSON snapshot of the whole
// dataset in CANONICAL units. JSON over a raw SQLite copy because it is decoupled from
// the schema version (a vN backup restores into vM through the repositories + migrations),
// human-inspectable, and round-trips through the same boundary the importers use. Pure:
// reads only through the repositories, the export timestamp comes from the injected Clock,
// returns the bytes (file IO lives at the GUI edge). Mirror image of BackupImporter.
namespace klr {

class IPlantRepository;
class ISensorRepository;
class IBindingRepository;
class IReadingRepository;
class IJournalRepository;
class IAttachmentRepository;
class ICareThresholdRepository;
class Clock;

class BackupSerializer {
public:
    // Bumped only on a breaking schema change; the importer refuses a newer version. The
    // `attachments` array (ADR 0024) is ADDITIVE — an older importer ignores the unknown key — so it
    // did NOT bump the version (keeping a new backup restorable by an older build, minus its photos).
    // NOTE the attachment FILE BYTES are not in the JSON (decision 7) — only the metadata rows.
    static constexpr int kFormatVersion = 1;

    // The attachment repository is optional (nullptr in builds/tests without photo storage); when
    // absent the `attachments` array is simply empty.
    BackupSerializer(IPlantRepository &plants, ISensorRepository &sensors,
                     IBindingRepository &bindings, IReadingRepository &readings,
                     IJournalRepository &journal, ICareThresholdRepository &thresholds,
                     const Clock &clock, IAttachmentRepository *attachments = nullptr);

    QByteArray toJson() const;

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
