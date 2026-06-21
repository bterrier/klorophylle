// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqlitesyncstaterepository.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

std::optional<QDateTime> SqliteSyncStateRepository::lastHistorySync(SensorId sensor) const
{
    QSqlQuery q(m_db.handle());
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT last_history_sync FROM sensor_sync_state WHERE sensor_id = :sid"));
    q.bindValue(QStringLiteral(":sid"), sensor.toString());
    detail::execPreparedOrThrow(q);
    if (!q.next())
        return std::nullopt;
    const QVariant v = q.value(0);
    if (v.isNull())
        return std::nullopt;
    return detail::fromIso(v.toString());
}

void SqliteSyncStateRepository::setLastHistorySync(SensorId sensor, const QDateTime &when)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(
            QStringLiteral("setLastHistorySync: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "INSERT INTO sensor_sync_state(sensor_id, last_history_sync) VALUES(:sid, :ts) "
            "ON CONFLICT(sensor_id) DO UPDATE SET last_history_sync = excluded.last_history_sync"));
        q.bindValue(QStringLiteral(":sid"), sensor.toString());
        q.bindValue(QStringLiteral(":ts"), detail::toIso(when));
        detail::execPreparedOrThrow(q);
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(
                QStringLiteral("setLastHistorySync: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

} // namespace klr
