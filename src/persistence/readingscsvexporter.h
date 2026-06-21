// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "units.h" // DisplayUnits

#include <QtCore/QDateTime>
#include <QtCore/QString>

// Readings CSV export (ADR 0010): a human/spreadsheet dump of reading history, in the
// user's DISPLAY units. Lossy by design — no ids, no bindings — so it is for analysis,
// NOT restore (that is BackupSerializer/BackupImporter). Pure: reads only through the
// repositories, takes the range + units in, returns the whole CSV as a QString (no file
// IO — that lives at the GUI edge in AppContext, like the legacy importer).
//
// Layout is tidy/long — ONE row per reading — not a wide grid: with 16
// quantities and readings from different sensors rarely sharing a timestamp, a wide grid
// is mostly empty. History is plant-facing (follows the plant across sensor swaps, ADR
// 0005), so the CSV shows what the PLANT experienced.
namespace klr {

class IPlantRepository;
class IBindingRepository;
class IReadingRepository;
class ISensorRepository;

class ReadingsCsvExporter {
public:
    ReadingsCsvExporter(IPlantRepository &plants, IBindingRepository &bindings,
                        IReadingRepository &readings, ISensorRepository &sensors);

    // Header: plant,species,sensor_model,timestamp,quantity,value,unit,provenance
    // Values are converted to `units`; absent values are an empty cell (never the -99
    // sentinel). Timestamps render ISO-8601 in local time with offset. An empty dataset
    // yields the header line only.
    QString exportCsv(const DisplayUnits &units, const QDateTime &from, const QDateTime &to) const;

private:
    IPlantRepository &m_plants;
    IBindingRepository &m_bindings;
    IReadingRepository &m_readings;
    ISensorRepository &m_sensors;
};

// RFC 4180 quoting: a field is wrapped in double-quotes (with embedded quotes doubled)
// iff it contains a comma, double-quote, CR or LF. Plant display names are user-typed,
// so this is not optional. Exposed for unit testing.
QString csvField(const QString &field);

} // namespace klr
