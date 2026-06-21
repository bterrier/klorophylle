// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtSql/QSqlQuery>

// Small fail-loud helpers shared by the SQLite repositories. Keeping these here
// means each repo reads as straight-line "bind, exec, log" with no error plumbing
// repeated at every call site. All throw StorageError on SQL failure.
namespace klr::detail {

void prepareOrThrow(QSqlQuery &q, const QString &sql);
void execPreparedOrThrow(QSqlQuery &q);     // exec an already-prepared + bound query
void execOrThrow(QSqlQuery &q, const QString &sql); // prepare-less literal statement (DDL)

QString toIso(const QDateTime &dt);         // UTC ISO-8601 with ms
QDateTime fromIso(const QString &s);        // parse back as UTC

// Appends one row to change_log on db's connection (so it shares the caller's open
// transaction). Stamps ts_utc + hlc_ms from the injected clock, hlc_counter 0, and
// the per-install replica_id. The HLC reducer / sync is deferred — this only keeps
// the log present and transactional.
void appendChangeLog(Database &db, const QString &entity, const QString &entityId,
                     const QString &op, const QJsonObject &payload);

} // namespace klr::detail
