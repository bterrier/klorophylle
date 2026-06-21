// SPDX-License-Identifier: GPL-3.0-or-later
#include "legacyimporter.h"

#include "clock.h"
#include "ibindingrepository.h"
#include "ijournalrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "journalentry.h"
#include "log.h"
#include "plant.h"
#include "reading.h"
#include "sensor.h"
#include "storageerror.h"

#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimeZone>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include <exception>
#include <utility>

namespace klr {

namespace {

// The legacy -99 "absent" sentinel. NULL is absent too.
bool isAbsent(const QVariant &v)
{
    if (v.isNull())
        return true;
    bool ok = false;
    const double d = v.toDouble(&ok);
    return !ok || d == -99.0;
}

// Map a legacy plantJournal.entryType (Journal::JournalType) to a klr kind.
JournalEntryKind mapJournalKind(int legacyType)
{
    switch (legacyType) {
    case 1:  return JournalEntryKind::Watering;    // JOURNAL_WATER
    case 2:  return JournalEntryKind::Fertilizing; // JOURNAL_FERTILIZE
    case 3:  return JournalEntryKind::Pruning;     // JOURNAL_PRUNE
    case 6:  return JournalEntryKind::Repotting;   // JOURNAL_REPOT
    case 7:  return JournalEntryKind::Observation; // JOURNAL_PHOTO — a stub with no path/blob; a
                                                   // photo is a sighting, so keep the comment as an
                                                   // Observation note (nothing else to lift).
    // JOURNAL_ROTATE (4) / JOURNAL_MOVE (5) are actions with no dedicated kind, JOURNAL_COMMENT (8)
    // and JOURNAL_UNKNOWN (0) carry no action — all fall through to the generic Note (comment kept).
    default: return JournalEntryKind::Note;
    }
}

// Read a row's sample time: prefer `timestamp`, fall back to `timestamp_rounded`.
QDateTime sampleTime(const QSqlQuery &q)
{
    QDateTime t = q.value(QStringLiteral("timestamp")).toDateTime();
    if (!t.isValid())
        t = q.value(QStringLiteral("timestamp_rounded")).toDateTime();
    if (t.isValid() && t.timeSpec() == Qt::LocalTime)
        t.setTimeZone(QTimeZone::UTC); // legacy stored naive local; treat as UTC
    return t;
}

// One wide history table → readings under a sensor, un-pivoting the named columns.
int importWideTable(IReadingRepository &readings, QSqlDatabase &src, SensorId sensor,
                    const QString &addr, const QString &table,
                    const QList<std::pair<const char *, Quantity>> &columns)
{
    QString cols = QStringLiteral("timestamp, timestamp_rounded");
    for (const auto &[name, q] : columns)
        cols += QStringLiteral(", ") + QLatin1String(name);

    QSqlQuery sel(src);
    sel.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE deviceAddr = :a").arg(cols, table));
    sel.bindValue(QStringLiteral(":a"), addr);
    if (!sel.exec())
        return 0;

    QList<Reading> batch;
    while (sel.next()) {
        const QDateTime t = sampleTime(sel);
        if (!t.isValid())
            continue;
        for (const auto &[name, quantity] : columns) {
            const QVariant v = sel.value(QLatin1String(name));
            if (isAbsent(v))
                continue; // -99 / NULL → not stored (never re-introduce the sentinel)
            batch.append(Reading{ quantity, v.toDouble(), canonicalUnit(quantity), t,
                                  Provenance::History });
        }
    }
    if (!batch.isEmpty())
        readings.append(sensor, batch);
    return int(batch.size());
}

// The legacy plantCache is a JSON snapshot of the catalog entry; its "name" field is the
// botanical species key (same value as plants.plantName). Used as a fallback species when
// plantName is empty. Returns empty on any parse failure.
QString cacheBotanicalName(const QString &cacheJson)
{
    if (cacheJson.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(cacheJson.toUtf8());
    if (!doc.isObject())
        return {};
    return doc.object().value(QStringLiteral("name")).toString();
}

bool hasTable(QSqlDatabase &db, const QString &name)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=:n"));
    q.bindValue(QStringLiteral(":n"), name);
    return q.exec() && q.next();
}

} // namespace

LegacyImporter::LegacyImporter(IPlantRepository &plants, ISensorRepository &sensors,
                               IBindingRepository &bindings, IReadingRepository &readings,
                               IJournalRepository &journal, const Clock &clock)
    : m_plants(plants)
    , m_sensors(sensors)
    , m_bindings(bindings)
    , m_readings(readings)
    , m_journal(journal)
    , m_clock(clock)
{
}

LegacyImporter::Result LegacyImporter::importFrom(const QString &legacyDbPath)
{
    Result result;
    const QDateTime nowUtc = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
    const QString conn = QStringLiteral("legacy-import-") + QUuid::createUuid().toString(QUuid::Id128);

    // All QSqlDatabase/QSqlQuery use stays inside this scope so it is destroyed BEFORE
    // removeDatabase() runs (else Qt warns the connection is still in use, mirroring the
    // Database class's no-long-lived-copy rule). The error is rethrown after cleanup.
    std::exception_ptr failure;
    {
        QSqlDatabase src = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        src.setDatabaseName(legacyDbPath);
        src.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        try {
            if (!src.open())
                throw StorageError(QStringLiteral("legacy import: cannot open %1: %2")
                                       .arg(legacyDbPath, src.lastError().text()));
            if (!hasTable(src, QStringLiteral("devices")))
                throw StorageError(QStringLiteral("legacy import: %1 is not a WatchFlower database "
                                                  "(no 'devices' table)").arg(legacyDbPath));

            // Pre-read plants(deviceAddr -> name/start/oldId) so a device gets its nicer
            // name + tracking start where one exists.
            // The legacy `plants` row carries the SPECIES (plants.plantName is the catalog
            // botanical key, plantCache its JSON snapshot) — NOT the user's plant name,
            // which lives in devices.associatedName.
            struct LegacyPlant {
                QString species;   // plants.plantName == catalog botanical key
                QString cacheName; // plantCache JSON "name" (fallback species)
                QDateTime start;
                int oldId = 0;
            };
            QHash<QString, LegacyPlant> plantByAddr; // deviceAddr -> plant
            QHash<int, QString> addrByOldPlantId;     // old plantId -> deviceAddr (for journal)
            if (hasTable(src, QStringLiteral("plants"))) {
                QSqlQuery pq(src);
                if (pq.exec(QStringLiteral("SELECT plantId, plantName, plantCache, plantStart, "
                                           "deviceAddr FROM plants"))) {
                    while (pq.next()) {
                        const QString addr = pq.value(QStringLiteral("deviceAddr")).toString();
                        if (addr.isEmpty())
                            continue;
                        LegacyPlant lp;
                        lp.oldId = pq.value(QStringLiteral("plantId")).toInt();
                        lp.species = pq.value(QStringLiteral("plantName")).toString();
                        lp.cacheName = cacheBotanicalName(pq.value(QStringLiteral("plantCache")).toString());
                        lp.start = pq.value(QStringLiteral("plantStart")).toDateTime();
                        plantByAddr.insert(addr, lp);
                        addrByOldPlantId.insert(lp.oldId, addr);
                    }
                }
            }

            // devices -> a Sensor + a Plant each + a synthesised open binding.
            QHash<QString, SensorId> sensorByAddr;
            QHash<QString, PlantId> plantIdByAddr;
            QSqlQuery dq(src);
            if (!dq.exec(QStringLiteral("SELECT deviceAddr, deviceAddrMAC, deviceName, deviceModel, "
                                        "associatedName, lastSeen FROM devices")))
                throw StorageError(QStringLiteral("legacy import: reading devices: %1").arg(dq.lastError().text()));
            while (dq.next()) {
                const QString addr = dq.value(QStringLiteral("deviceAddr")).toString();
                if (addr.isEmpty())
                    continue;
                const QString mac = dq.value(QStringLiteral("deviceAddrMAC")).toString();
                const QString model = dq.value(QStringLiteral("deviceModel")).toString();
                const QString handle = !mac.isEmpty() ? mac : addr;

                const SensorId sensor = m_sensors.ensure(HandleKind::Mac, handle, model);
                sensorByAddr.insert(addr, sensor);
                ++result.sensors;

                const LegacyPlant lp = plantByAddr.value(addr);
                // The user's chosen plant name is devices.associatedName; fall back to the
                // device's own name/model only when the user never set one.
                QString name = dq.value(QStringLiteral("associatedName")).toString();
                if (name.isEmpty())
                    name = dq.value(QStringLiteral("deviceName")).toString();
                if (name.isEmpty())
                    name = !model.isEmpty() ? model : addr;
                // Species is the legacy plants.plantName (catalog key), or its cache name.
                const QString species = (!lp.species.isEmpty() ? lp.species : lp.cacheName).trimmed();

                QDateTime start = lp.start;
                if (!start.isValid())
                    start = dq.value(QStringLiteral("lastSeen")).toDateTime();
                if (!start.isValid())
                    start = nowUtc;
                if (start.timeSpec() == Qt::LocalTime)
                    start.setTimeZone(QTimeZone::UTC);

                Plant plant;
                plant.id = PlantId::generate();
                plant.displayName = name;
                plant.species = species;
                plant.trackedSince = start;
                m_plants.add(plant);
                plantIdByAddr.insert(addr, plant.id);
                ++result.plants;

                m_bindings.bind(plant.id, sensor, start, std::nullopt);
                ++result.bindings;
            }

            // plantJournal -> journal entries under the device's plant.
            if (hasTable(src, QStringLiteral("plantJournal"))) {
                QSqlQuery jq(src);
                if (jq.exec(QStringLiteral("SELECT entryType, entryTimestamp, entryComment, plantId "
                                           "FROM plantJournal"))) {
                    while (jq.next()) {
                        const int oldPlantId = jq.value(QStringLiteral("plantId")).toInt();
                        const QString addr = addrByOldPlantId.value(oldPlantId);
                        const auto it = plantIdByAddr.constFind(addr);
                        if (it == plantIdByAddr.cend())
                            continue; // orphan journal entry — its device/plant is gone
                        QDateTime ts = jq.value(QStringLiteral("entryTimestamp")).toDateTime();
                        if (ts.timeSpec() == Qt::LocalTime)
                            ts.setTimeZone(QTimeZone::UTC);
                        JournalEntry e;
                        e.id = JournalEntryId::generate();
                        e.plant = *it;
                        e.timestamp = ts.isValid() ? ts : nowUtc;
                        e.kind = mapJournalKind(jq.value(QStringLiteral("entryType")).toInt());
                        e.note = jq.value(QStringLiteral("entryComment")).toString();
                        m_journal.add(e);
                        ++result.journalEntries;
                    }
                }
            }

            // plantData / thermoData / sensorData -> readings (un-pivoted, -99 dropped).
            const QList<std::pair<const char *, Quantity>> plantCols = {
                { "soilMoisture", Quantity::SoilMoisture },
                { "soilConductivity", Quantity::SoilConductivity },
                { "soilTemperature", Quantity::SoilTemperature },
                { "temperature", Quantity::AirTemperature },
                { "humidity", Quantity::AirHumidity },
                { "luminosity", Quantity::Illuminance },
                { "watertank", Quantity::WaterTank },
            };
            const QList<std::pair<const char *, Quantity>> thermoCols = {
                { "temperature", Quantity::AirTemperature },
                { "humidity", Quantity::AirHumidity },
                { "pressure", Quantity::Pressure },
            };
            const QList<std::pair<const char *, Quantity>> sensorCols = {
                { "temperature", Quantity::AirTemperature },
                { "humidity", Quantity::AirHumidity },
                { "pressure", Quantity::Pressure },
                { "luminosity", Quantity::Illuminance },
                { "water", Quantity::WaterTank },
                { "pm25", Quantity::Pm25 },
                { "pm10", Quantity::Pm10 },
                { "co2", Quantity::Co2 },
                { "voc", Quantity::Voc },
                { "hcho", Quantity::Hcho },
                { "radioactivity", Quantity::Radioactivity },
            };

            for (auto it = sensorByAddr.constBegin(); it != sensorByAddr.constEnd(); ++it) {
                const QString &addr = it.key();
                const SensorId sensor = it.value();
                if (hasTable(src, QStringLiteral("plantData")))
                    result.readings += importWideTable(m_readings, src, sensor, addr,
                                                        QStringLiteral("plantData"), plantCols);
                if (hasTable(src, QStringLiteral("thermoData")))
                    result.readings += importWideTable(m_readings, src, sensor, addr,
                                                        QStringLiteral("thermoData"), thermoCols);
                if (hasTable(src, QStringLiteral("sensorData")))
                    result.readings += importWideTable(m_readings, src, sensor, addr,
                                                        QStringLiteral("sensorData"), sensorCols);
            }
        } catch (...) {
            failure = std::current_exception();
        }
    } // src + all QSqlQuery destroyed here
    QSqlDatabase::removeDatabase(conn);

    if (failure)
        std::rethrow_exception(failure);

    qCInfo(lcApp) << "legacy import:" << result.sensors << "sensors" << result.plants << "plants"
                  << result.bindings << "bindings" << result.journalEntries << "journal"
                  << result.readings << "readings";
    return result;
}

} // namespace klr
