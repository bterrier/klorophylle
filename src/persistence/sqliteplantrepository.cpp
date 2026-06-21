// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqliteplantrepository.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QJsonObject>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

namespace {

QJsonObject plantPayload(const Plant &p)
{
    return QJsonObject{
        { QStringLiteral("id"), p.id.toString() },
        { QStringLiteral("displayName"), p.displayName },
        { QStringLiteral("species"), p.species },
        { QStringLiteral("trackedSince"), detail::toIso(p.trackedSince) },
    };
}

Plant plantFromQuery(const QSqlQuery &q)
{
    Plant p;
    p.id = PlantId{ QUuid::fromString(q.value(QStringLiteral("id")).toString()) };
    p.displayName = q.value(QStringLiteral("display_name")).toString();
    p.species = q.value(QStringLiteral("species")).toString();
    p.trackedSince = detail::fromIso(q.value(QStringLiteral("tracked_since")).toString());
    return p;
}

} // namespace

void SqlitePlantRepository::add(const Plant &plant)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("plant add: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "INSERT INTO plants(id, display_name, species, tracked_since) "
            "VALUES(:id, :name, COALESCE(:species, ''), :ts)")); // null species -> ''
        q.bindValue(QStringLiteral(":id"), plant.id.toString());
        q.bindValue(QStringLiteral(":name"), plant.displayName);
        q.bindValue(QStringLiteral(":species"), plant.species);
        q.bindValue(QStringLiteral(":ts"), detail::toIso(plant.trackedSince));
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("plant"), plant.id.toString(),
                                QStringLiteral("insert"), plantPayload(plant));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("plant add: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqlitePlantRepository::update(const Plant &plant)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("plant update: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "UPDATE plants SET display_name = :name, species = COALESCE(:species, ''), "
            "tracked_since = :ts WHERE id = :id"));
        q.bindValue(QStringLiteral(":name"), plant.displayName);
        q.bindValue(QStringLiteral(":species"), plant.species);
        q.bindValue(QStringLiteral(":ts"), detail::toIso(plant.trackedSince));
        q.bindValue(QStringLiteral(":id"), plant.id.toString());
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("plant"), plant.id.toString(),
                                QStringLiteral("update"), plantPayload(plant));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("plant update: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqlitePlantRepository::remove(PlantId id)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("plant remove: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral("DELETE FROM plants WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), id.toString());
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("plant"), id.toString(),
                                QStringLiteral("delete"),
                                QJsonObject{ { QStringLiteral("id"), id.toString() } });
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("plant remove: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

std::optional<Plant> SqlitePlantRepository::get(PlantId id) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, display_name, species, tracked_since FROM plants WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id.toString());
    detail::execPreparedOrThrow(q);
    if (!q.next())
        return std::nullopt;
    return plantFromQuery(q);
}

QList<Plant> SqlitePlantRepository::all() const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::execOrThrow(q, QStringLiteral(
        "SELECT id, display_name, species, tracked_since FROM plants "
        "ORDER BY tracked_since, id"));
    QList<Plant> out;
    while (q.next())
        out.append(plantFromQuery(q));
    return out;
}

} // namespace klr
