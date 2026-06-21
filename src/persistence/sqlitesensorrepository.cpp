// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqlitesensorrepository.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QJsonObject>
#include <QtCore/QTimeZone>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

namespace {

QJsonObject sensorPayload(const Sensor &s)
{
    return QJsonObject{
        { QStringLiteral("id"), s.id.toString() },
        { QStringLiteral("model"), s.model },
        { QStringLiteral("handleKind"), static_cast<int>(s.handleKind) },
        { QStringLiteral("handleValue"), s.handleValue },
        { QStringLiteral("firstSeen"), detail::toIso(s.firstSeen) },
    };
}

Sensor sensorFromQuery(const QSqlQuery &q)
{
    Sensor s;
    s.id = SensorId{ QUuid::fromString(q.value(QStringLiteral("id")).toString()) };
    s.model = q.value(QStringLiteral("model")).toString();
    s.handleKind = static_cast<HandleKind>(q.value(QStringLiteral("handle_kind")).toInt());
    s.handleValue = q.value(QStringLiteral("handle_value")).toString();
    s.firstSeen = detail::fromIso(q.value(QStringLiteral("first_seen")).toString());
    return s;
}

} // namespace

SensorId SqliteSensorRepository::ensure(HandleKind kind, const QString &handleValue,
                                        const QString &model)
{
    if (const std::optional<Sensor> existing = findByHandle(kind, handleValue))
        return existing->id; // dedup on the handle — same physical sensor

    Sensor s;
    s.id = SensorId::generate();
    s.model = model;
    s.handleKind = kind;
    s.handleValue = handleValue;
    s.firstSeen = QDateTime::fromMSecsSinceEpoch(m_db.nowMs(), QTimeZone::UTC);

    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("sensor ensure: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "INSERT INTO sensors(id, model, handle_kind, handle_value, first_seen) "
            "VALUES(:id, :model, :kind, :handle, :ts)"));
        q.bindValue(QStringLiteral(":id"), s.id.toString());
        q.bindValue(QStringLiteral(":model"), s.model);
        q.bindValue(QStringLiteral(":kind"), static_cast<int>(s.handleKind));
        q.bindValue(QStringLiteral(":handle"), s.handleValue);
        q.bindValue(QStringLiteral(":ts"), detail::toIso(s.firstSeen));
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("sensor"), s.id.toString(),
                                QStringLiteral("insert"), sensorPayload(s));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("sensor ensure: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
    return s.id;
}

void SqliteSensorRepository::add(const Sensor &sensor)
{
    // Upsert by id so restore preserves the backup's SensorId (bindings/readings key on
    // it) and re-importing is idempotent: update when the id already exists, else insert.
    const bool exists = get(sensor.id).has_value();

    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("sensor add: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        if (exists) {
            detail::prepareOrThrow(q, QStringLiteral(
                "UPDATE sensors SET model = :model, handle_kind = :kind, "
                "handle_value = :handle, first_seen = :ts WHERE id = :id"));
        } else {
            detail::prepareOrThrow(q, QStringLiteral(
                "INSERT INTO sensors(id, model, handle_kind, handle_value, first_seen) "
                "VALUES(:id, :model, :kind, :handle, :ts)"));
        }
        q.bindValue(QStringLiteral(":id"), sensor.id.toString());
        q.bindValue(QStringLiteral(":model"), sensor.model);
        q.bindValue(QStringLiteral(":kind"), static_cast<int>(sensor.handleKind));
        q.bindValue(QStringLiteral(":handle"), sensor.handleValue);
        q.bindValue(QStringLiteral(":ts"), detail::toIso(sensor.firstSeen));
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("sensor"), sensor.id.toString(),
                                exists ? QStringLiteral("update") : QStringLiteral("insert"),
                                sensorPayload(sensor));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("sensor add: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

std::optional<Sensor> SqliteSensorRepository::get(SensorId id) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, model, handle_kind, handle_value, first_seen FROM sensors WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id.toString());
    detail::execPreparedOrThrow(q);
    if (!q.next())
        return std::nullopt;
    return sensorFromQuery(q);
}

std::optional<Sensor> SqliteSensorRepository::findByHandle(HandleKind kind,
                                                           const QString &handleValue) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, model, handle_kind, handle_value, first_seen FROM sensors "
        "WHERE handle_kind = :kind AND handle_value = :handle"));
    q.bindValue(QStringLiteral(":kind"), static_cast<int>(kind));
    q.bindValue(QStringLiteral(":handle"), handleValue);
    detail::execPreparedOrThrow(q);
    if (!q.next())
        return std::nullopt;
    return sensorFromQuery(q);
}

void SqliteSensorRepository::remove(SensorId id)
{
    // Delete the sensor row; the schema FKs cascade its readings + bindings. Mirrors
    // SqlitePlantRepository::remove: the mutation + its change_log row in one transaction.
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("sensor remove: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral("DELETE FROM sensors WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), id.toString());
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("sensor"), id.toString(),
                                QStringLiteral("delete"),
                                QJsonObject{ { QStringLiteral("id"), id.toString() } });
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("sensor remove: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

QList<Sensor> SqliteSensorRepository::all() const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::execOrThrow(q, QStringLiteral(
        "SELECT id, model, handle_kind, handle_value, first_seen FROM sensors "
        "ORDER BY first_seen, id"));
    QList<Sensor> out;
    while (q.next())
        out.append(sensorFromQuery(q));
    return out;
}

} // namespace klr
