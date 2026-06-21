// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqlitecarethresholdrepository.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

namespace {

QVariant boundToVariant(std::optional<double> v)
{
    return v.has_value() ? QVariant(*v) : QVariant(QMetaType(QMetaType::Double));
}

CareRange rangeFromQuery(const QSqlQuery &q)
{
    CareRange r;
    r.quantity = static_cast<Quantity>(q.value(QStringLiteral("quantity")).toInt());
    const QVariant lo = q.value(QStringLiteral("min_value"));
    const QVariant hi = q.value(QStringLiteral("max_value"));
    if (!lo.isNull())
        r.min = lo.toDouble();
    if (!hi.isNull())
        r.max = hi.toDouble();
    return r;
}

} // namespace

QList<CareRange> SqliteCareThresholdRepository::thresholdsFor(PlantId plant) const
{
    QSqlQuery q(m_db.handle());
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT quantity, min_value, max_value FROM care_thresholds "
        "WHERE plant_id = :plant ORDER BY quantity"));
    q.bindValue(QStringLiteral(":plant"), plant.toString());
    detail::execPreparedOrThrow(q);
    QList<CareRange> out;
    while (q.next())
        out.append(rangeFromQuery(q));
    return out;
}

void SqliteCareThresholdRepository::logSet(PlantId plant, const QString &op)
{
    QJsonArray arr;
    for (const CareRange &r : thresholdsFor(plant)) {
        QJsonObject o{ { QStringLiteral("quantity"), static_cast<int>(r.quantity) } };
        o.insert(QStringLiteral("min"), r.min ? QJsonValue(*r.min) : QJsonValue());
        o.insert(QStringLiteral("max"), r.max ? QJsonValue(*r.max) : QJsonValue());
        arr.append(o);
    }
    detail::appendChangeLog(m_db, QStringLiteral("careThresholds"), plant.toString(), op,
                            QJsonObject{ { QStringLiteral("plant"), plant.toString() },
                                         { QStringLiteral("ranges"), arr } });
}

void SqliteCareThresholdRepository::setRange(PlantId plant, const CareRange &range)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("setRange: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        if (range.isSet()) {
            detail::prepareOrThrow(q, QStringLiteral(
                "INSERT INTO care_thresholds(plant_id, quantity, min_value, max_value) "
                "VALUES(:plant, :q, :min, :max) "
                "ON CONFLICT(plant_id, quantity) DO UPDATE SET "
                "  min_value = excluded.min_value, max_value = excluded.max_value"));
            q.bindValue(QStringLiteral(":plant"), plant.toString());
            q.bindValue(QStringLiteral(":q"), static_cast<int>(range.quantity));
            q.bindValue(QStringLiteral(":min"), boundToVariant(range.min));
            q.bindValue(QStringLiteral(":max"), boundToVariant(range.max));
        } else {
            // An unset range deletes the row (nothing to judge against).
            detail::prepareOrThrow(q, QStringLiteral(
                "DELETE FROM care_thresholds WHERE plant_id = :plant AND quantity = :q"));
            q.bindValue(QStringLiteral(":plant"), plant.toString());
            q.bindValue(QStringLiteral(":q"), static_cast<int>(range.quantity));
        }
        detail::execPreparedOrThrow(q);
        logSet(plant, QStringLiteral("update"));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("setRange: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqliteCareThresholdRepository::replaceAll(PlantId plant, std::span<const CareRange> ranges)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(
            QStringLiteral("replaceAll: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery del(d);
        detail::prepareOrThrow(del,
                               QStringLiteral("DELETE FROM care_thresholds WHERE plant_id = :plant"));
        del.bindValue(QStringLiteral(":plant"), plant.toString());
        detail::execPreparedOrThrow(del);

        for (const CareRange &r : ranges) {
            if (!r.isSet())
                continue;
            QSqlQuery ins(d);
            detail::prepareOrThrow(ins, QStringLiteral(
                "INSERT INTO care_thresholds(plant_id, quantity, min_value, max_value) "
                "VALUES(:plant, :q, :min, :max)"));
            ins.bindValue(QStringLiteral(":plant"), plant.toString());
            ins.bindValue(QStringLiteral(":q"), static_cast<int>(r.quantity));
            ins.bindValue(QStringLiteral(":min"), boundToVariant(r.min));
            ins.bindValue(QStringLiteral(":max"), boundToVariant(r.max));
            detail::execPreparedOrThrow(ins);
        }
        logSet(plant, QStringLiteral("update"));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("replaceAll: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqliteCareThresholdRepository::clear(PlantId plant)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("clear: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q,
                               QStringLiteral("DELETE FROM care_thresholds WHERE plant_id = :plant"));
        q.bindValue(QStringLiteral(":plant"), plant.toString());
        detail::execPreparedOrThrow(q);
        logSet(plant, QStringLiteral("delete")); // resulting set is now empty
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("clear: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

} // namespace klr
