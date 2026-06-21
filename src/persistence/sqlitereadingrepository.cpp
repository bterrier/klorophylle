// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqlitereadingrepository.h"
#include "bucket.h"
#include "readingattribution.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QTimeZone>
#include <QtCore/QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

namespace {

Reading readingFromQuery(const QSqlQuery &q)
{
    Reading r;
    r.quantity = static_cast<Quantity>(q.value(QStringLiteral("quantity")).toInt());
    const QVariant v = q.value(QStringLiteral("value"));
    if (!v.isNull())
        r.value = v.toDouble();
    r.unit = canonicalUnit(r.quantity); // not stored — derived from the quantity
    r.timestamp = detail::fromIso(q.value(QStringLiteral("ts_utc")).toString());
    r.provenance = static_cast<Provenance>(q.value(QStringLiteral("source")).toInt());
    return r;
}

} // namespace

void SqliteReadingRepository::append(SensorId sensor, std::span<const Reading> readings)
{
    QSqlDatabase d = m_db.handle();
    // The node that saw these readings — stamped on live/advertisement rows for the
    // probe/sync substrate (goal #4/#5). Imported History rows were not observed here.
    const QString replica = m_db.replicaId();
    if (!d.transaction())
        throw StorageError(QStringLiteral("reading append: begin failed: %1").arg(d.lastError().text()));
    try {
        for (const Reading &r : readings) {
            QSqlQuery q(d);
            // One row per (sensor, quantity, bucket): ts_bucket floors the reading's own
            // timestamp to the bucket boundary (ADR 0006). On conflict the latest sample
            // wins — but an absent (NULL) value never erases a stored present one
            // (WHERE excluded.value IS NOT NULL): absence is not news.
            detail::prepareOrThrow(q, QStringLiteral(
                "INSERT INTO readings(sensor_id, quantity, ts_utc, ts_bucket, value, source, observed_by) "
                "VALUES(:sid, :q, :ts, :bucket, :val, :src, :obs) "
                "ON CONFLICT(sensor_id, quantity, ts_bucket) DO UPDATE SET "
                "value = excluded.value, ts_utc = excluded.ts_utc, "
                "source = excluded.source, observed_by = excluded.observed_by "
                "WHERE excluded.value IS NOT NULL"));
            const qint64 tsMs = r.timestamp.toMSecsSinceEpoch();
            const QString bucket =
                detail::toIso(QDateTime::fromMSecsSinceEpoch(bucketStartMs(tsMs), QTimeZone::UTC));
            q.bindValue(QStringLiteral(":sid"), sensor.toString());
            q.bindValue(QStringLiteral(":q"), static_cast<int>(r.quantity));
            q.bindValue(QStringLiteral(":ts"), detail::toIso(r.timestamp));
            q.bindValue(QStringLiteral(":bucket"), bucket);
            q.bindValue(QStringLiteral(":val"), r.value.has_value() ? QVariant(*r.value) : QVariant());
            q.bindValue(QStringLiteral(":src"), static_cast<int>(r.provenance));
            // Empty (not NULL) for imported rows — the column is NOT NULL; a default-
            // constructed QString would bind as SQL NULL.
            q.bindValue(QStringLiteral(":obs"),
                        r.provenance == Provenance::History ? QStringLiteral("") : replica);
            detail::execPreparedOrThrow(q);
        }
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("reading append: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

QList<Reading> SqliteReadingRepository::history(SensorId sensor, Quantity quantity,
                                                const QDateTime &from, const QDateTime &to) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT quantity, ts_utc, value, source FROM readings "
        "WHERE sensor_id = :sid AND quantity = :q AND ts_utc >= :from AND ts_utc <= :to "
        "ORDER BY ts_utc"));
    q.bindValue(QStringLiteral(":sid"), sensor.toString());
    q.bindValue(QStringLiteral(":q"), static_cast<int>(quantity));
    q.bindValue(QStringLiteral(":from"), detail::toIso(from));
    q.bindValue(QStringLiteral(":to"), detail::toIso(to));
    detail::execPreparedOrThrow(q);
    QList<Reading> out;
    while (q.next())
        out.append(readingFromQuery(q));
    return out;
}

std::optional<Reading> SqliteReadingRepository::latest(SensorId sensor, Quantity quantity) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT quantity, ts_utc, value, source FROM readings "
        "WHERE sensor_id = :sid AND quantity = :q AND value IS NOT NULL "
        "ORDER BY ts_utc DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":sid"), sensor.toString());
    q.bindValue(QStringLiteral(":q"), static_cast<int>(quantity));
    detail::execPreparedOrThrow(q);
    if (!q.next())
        return std::nullopt;
    return readingFromQuery(q);
}

QList<Reading> SqliteReadingRepository::seriesForPlant(std::span<const PlantSensorBinding> bindings,
                                                       Quantity quantity, const QDateTime &from,
                                                       const QDateTime &to) const
{
    return detail::seriesForPlant(
        bindings, quantity, from, to,
        [this](SensorId s, Quantity q, const QDateTime &f, const QDateTime &t) {
            return history(s, q, f, t);
        });
}

QList<Reading> SqliteReadingRepository::currentForPlant(
    std::span<const PlantSensorBinding> bindings) const
{
    return detail::currentForPlant(
        bindings, [this](SensorId s, Quantity q) { return latest(s, q); });
}

void SqliteReadingRepository::removeForSensor(SensorId sensor)
{
    // Bulk telemetry carries no change_log (see append), so neither does this cleanup.
    // The schema would also cascade these on the sensor delete; clearing them here keeps
    // the SensorDeleter's one code path uniform with the in-memory fake.
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(
            QStringLiteral("reading removeForSensor: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral("DELETE FROM readings WHERE sensor_id = :sid"));
        q.bindValue(QStringLiteral(":sid"), sensor.toString());
        detail::execPreparedOrThrow(q);
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(
                QStringLiteral("reading removeForSensor: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

} // namespace klr
