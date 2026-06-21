// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

// Brings an existing WatchFlower `data.db` forward into the klorophylle schema. It reads
// the legacy file READ-ONLY and writes through the repositories (so the change-log + FK
// discipline hold), mapping: devices -> sensors + a plant each + a synthesised binding;
// plants/plantJournal -> nicer plant names + journal; plantData/thermoData/sensorData ->
// readings, un-pivoting the wide rows to (quantity, value) and dropping the -99 sentinel
// to absent. See ../../docs/adr/0006-history-charts-import.md.
//
// Honest limitation: a DB already upgraded through the old destructive v2->v3 sensorData
// drop has lost that environmental history — the importer cannot recover what isn't there.
namespace klr {

class IPlantRepository;
class ISensorRepository;
class IBindingRepository;
class IReadingRepository;
class IJournalRepository;
class Clock;

class LegacyImporter {
public:
    struct Result {
        int sensors = 0;
        int plants = 0;
        int bindings = 0;
        int journalEntries = 0;
        int readings = 0;
        QStringList warnings;
    };

    LegacyImporter(IPlantRepository &plants, ISensorRepository &sensors,
                   IBindingRepository &bindings, IReadingRepository &readings,
                   IJournalRepository &journal, const Clock &clock);

    // Open `legacyDbPath` read-only and import it. Throws StorageError if the file
    // cannot be opened as a WatchFlower database (missing `devices` table).
    Result importFrom(const QString &legacyDbPath);

private:
    IPlantRepository &m_plants;
    ISensorRepository &m_sensors;
    IBindingRepository &m_bindings;
    IReadingRepository &m_readings;
    IJournalRepository &m_journal;
    const Clock &m_clock;
};

} // namespace klr
