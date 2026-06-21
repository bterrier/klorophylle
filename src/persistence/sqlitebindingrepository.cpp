// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqlitebindingrepository.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QJsonObject>
#include <QtCore/QStringList>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include <span>

namespace klr {

namespace {

// role: NULL <-> nullopt; valid_to: NULL <-> currently bound.
QVariant roleToVariant(std::optional<Quantity> role)
{
    return role.has_value() ? QVariant(static_cast<int>(*role)) : QVariant();
}

QVariant validToToVariant(const std::optional<QDateTime> &to)
{
    return to.has_value() ? QVariant(detail::toIso(*to)) : QVariant();
}

PlantSensorBinding bindingFromQuery(const QSqlQuery &q)
{
    PlantSensorBinding b;
    b.plant = PlantId{ QUuid::fromString(q.value(QStringLiteral("plant_id")).toString()) };
    b.sensor = SensorId{ QUuid::fromString(q.value(QStringLiteral("sensor_id")).toString()) };
    b.validFrom = detail::fromIso(q.value(QStringLiteral("valid_from")).toString());

    const QVariant to = q.value(QStringLiteral("valid_to"));
    if (!to.isNull())
        b.validTo = detail::fromIso(to.toString());

    const QVariant role = q.value(QStringLiteral("role"));
    if (!role.isNull())
        b.role = static_cast<Quantity>(role.toInt());
    return b;
}

QJsonObject bindingPayload(const QString &id, const PlantSensorBinding &b)
{
    return QJsonObject{
        { QStringLiteral("id"), id },
        { QStringLiteral("plant"), b.plant.toString() },
        { QStringLiteral("sensor"), b.sensor.toString() },
        { QStringLiteral("validFrom"), detail::toIso(b.validFrom) },
        { QStringLiteral("validTo"), b.validTo ? detail::toIso(*b.validTo) : QString() },
        { QStringLiteral("role"), b.role ? static_cast<int>(*b.role) : -1 },
    };
}

} // namespace

void SqliteBindingRepository::bind(PlantId plant, SensorId sensor, const QDateTime &validFrom,
                                   std::optional<Quantity> role)
{
    const QList<PlantSensorBinding> existing = bindings(plant);
    const PlantSensorBinding candidate{ plant, sensor, validFrom, std::nullopt, role };
    const auto ok = validateBinding(
        std::span<const PlantSensorBinding>(existing.constData(), existing.size()), candidate);
    if (!ok)
        throw StorageError(QStringLiteral("binding rejected: role conflict for this plant"));

    const QString id = QUuid::createUuidV7().toString(QUuid::WithoutBraces);

    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("bind: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "INSERT INTO plant_sensor_bindings(id, plant_id, sensor_id, valid_from, valid_to, role) "
            "VALUES(:id, :plant, :sensor, :from, :to, :role)"));
        q.bindValue(QStringLiteral(":id"), id);
        q.bindValue(QStringLiteral(":plant"), plant.toString());
        q.bindValue(QStringLiteral(":sensor"), sensor.toString());
        q.bindValue(QStringLiteral(":from"), detail::toIso(validFrom));
        q.bindValue(QStringLiteral(":to"), validToToVariant(std::nullopt));
        q.bindValue(QStringLiteral(":role"), roleToVariant(role));
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("binding"), id,
                                QStringLiteral("insert"), bindingPayload(id, candidate));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("bind: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqliteBindingRepository::unbind(PlantId plant, SensorId sensor, const QDateTime &validTo)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("unbind: begin failed: %1").arg(d.lastError().text()));
    try {
        // Find the open edge(s) for this pair first, so each gets its own change_log row.
        QStringList openIds;
        {
            QSqlQuery sel(d);
            detail::prepareOrThrow(sel, QStringLiteral(
                "SELECT id FROM plant_sensor_bindings "
                "WHERE plant_id = :plant AND sensor_id = :sensor AND valid_to IS NULL"));
            sel.bindValue(QStringLiteral(":plant"), plant.toString());
            sel.bindValue(QStringLiteral(":sensor"), sensor.toString());
            detail::execPreparedOrThrow(sel);
            while (sel.next())
                openIds << sel.value(0).toString();
        }

        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "UPDATE plant_sensor_bindings SET valid_to = :to "
            "WHERE plant_id = :plant AND sensor_id = :sensor AND valid_to IS NULL"));
        q.bindValue(QStringLiteral(":to"), detail::toIso(validTo));
        q.bindValue(QStringLiteral(":plant"), plant.toString());
        q.bindValue(QStringLiteral(":sensor"), sensor.toString());
        detail::execPreparedOrThrow(q);

        for (const QString &id : std::as_const(openIds)) {
            detail::appendChangeLog(m_db, QStringLiteral("binding"), id, QStringLiteral("update"),
                                    QJsonObject{ { QStringLiteral("id"), id },
                                                 { QStringLiteral("validTo"), detail::toIso(validTo) } });
        }
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("unbind: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

QList<PlantSensorBinding> SqliteBindingRepository::activeFor(PlantId plant,
                                                            const QDateTime &at) const
{
    const QList<PlantSensorBinding> forPlant = bindings(plant);
    return activeBindings(std::span<const PlantSensorBinding>(forPlant.constData(), forPlant.size()),
                          at);
}

QList<PlantSensorBinding> SqliteBindingRepository::bindingsForSensor(SensorId sensor) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT plant_id, sensor_id, valid_from, valid_to, role FROM plant_sensor_bindings "
        "WHERE sensor_id = :sensor ORDER BY valid_from, id"));
    q.bindValue(QStringLiteral(":sensor"), sensor.toString());
    detail::execPreparedOrThrow(q);
    QList<PlantSensorBinding> out;
    while (q.next())
        out.append(bindingFromQuery(q));
    return out;
}

void SqliteBindingRepository::removeForSensor(SensorId sensor)
{
    // Silent cleanup that accompanies a sensor delete (the cascade equivalent — the
    // schema would also clear these on the sensor row's ON DELETE CASCADE). No
    // change_log: the sensor delete is the logged root of the removal.
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(
            QStringLiteral("binding removeForSensor: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(
            q, QStringLiteral("DELETE FROM plant_sensor_bindings WHERE sensor_id = :sensor"));
        q.bindValue(QStringLiteral(":sensor"), sensor.toString());
        detail::execPreparedOrThrow(q);
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(
                QStringLiteral("binding removeForSensor: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

QList<PlantSensorBinding> SqliteBindingRepository::bindings(PlantId plant) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT plant_id, sensor_id, valid_from, valid_to, role FROM plant_sensor_bindings "
        "WHERE plant_id = :plant ORDER BY valid_from, id"));
    q.bindValue(QStringLiteral(":plant"), plant.toString());
    detail::execPreparedOrThrow(q);
    QList<PlantSensorBinding> out;
    while (q.next())
        out.append(bindingFromQuery(q));
    return out;
}

} // namespace klr
