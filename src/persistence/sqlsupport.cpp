// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QTimeZone>
#include <QtSql/QSqlError>

namespace klr::detail {

void prepareOrThrow(QSqlQuery &q, const QString &sql)
{
    if (!q.prepare(sql))
        throw StorageError(QStringLiteral("prepare failed: %1: %2").arg(sql, q.lastError().text()));
}

void execPreparedOrThrow(QSqlQuery &q)
{
    if (!q.exec())
        throw StorageError(QStringLiteral("exec failed: %1: %2")
                               .arg(q.lastQuery(), q.lastError().text()));
}

void execOrThrow(QSqlQuery &q, const QString &sql)
{
    if (!q.exec(sql))
        throw StorageError(QStringLiteral("exec failed: %1: %2").arg(sql, q.lastError().text()));
}

QString toIso(const QDateTime &dt)
{
    return dt.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime fromIso(const QString &s)
{
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    dt.setTimeZone(QTimeZone::UTC);
    return dt;
}

void appendChangeLog(Database &db, const QString &entity, const QString &entityId,
                     const QString &op, const QJsonObject &payload)
{
    const qint64 ms = db.nowMs();
    QSqlDatabase d = db.handle();
    QSqlQuery q(d);
    prepareOrThrow(q, QStringLiteral(
        "INSERT INTO change_log(entity, entity_id, op, ts_utc, hlc_ms, hlc_counter, "
        "replica_id, payload_json) VALUES(:e, :id, :op, :ts, :ms, 0, :rep, :payload)"));
    q.bindValue(QStringLiteral(":e"), entity);
    q.bindValue(QStringLiteral(":id"), entityId);
    q.bindValue(QStringLiteral(":op"), op);
    q.bindValue(QStringLiteral(":ts"), toIso(QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC)));
    q.bindValue(QStringLiteral(":ms"), ms);
    q.bindValue(QStringLiteral(":rep"), db.replicaId());
    q.bindValue(QStringLiteral(":payload"),
                QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    execPreparedOrThrow(q);
}

} // namespace klr::detail
