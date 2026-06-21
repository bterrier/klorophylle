// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqlitejournalrepository.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QJsonObject>
#include <QtCore/QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

namespace {

QJsonObject journalPayload(const JournalEntry &e)
{
    QJsonObject o{
        { QStringLiteral("id"), e.id.toString() },
        { QStringLiteral("timestamp"), detail::toIso(e.timestamp) },
        { QStringLiteral("kind"), static_cast<int>(e.kind) },
        { QStringLiteral("note"), e.note },
    };
    // plant rides along only for plant-scoped entries (nullopt == a global entry; ADR 0022).
    if (e.plant)
        o.insert(QStringLiteral("plant"), e.plant->toString());
    // editedAt rides along only when set (nullopt == never edited; ADR 0020 sync nuance).
    if (e.editedAt)
        o.insert(QStringLiteral("edited"), detail::toIso(*e.editedAt));
    return o;
}

JournalEntry entryFromQuery(const QSqlQuery &q)
{
    JournalEntry e;
    e.id = JournalEntryId{ QUuid::fromString(q.value(QStringLiteral("id")).toString()) };
    const QVariant plant = q.value(QStringLiteral("plant_id"));
    if (!plant.isNull())                        // NULL == a global (plant-less) entry
        e.plant = PlantId{ QUuid::fromString(plant.toString()) };
    e.timestamp = detail::fromIso(q.value(QStringLiteral("ts_utc")).toString());
    e.kind = static_cast<JournalEntryKind>(q.value(QStringLiteral("kind")).toInt());
    e.note = q.value(QStringLiteral("note")).toString();
    const QVariant edited = q.value(QStringLiteral("ts_edited"));
    if (!edited.isNull())                       // NULL == never edited
        e.editedAt = detail::fromIso(edited.toString());
    return e;
}

} // namespace

void SqliteJournalRepository::add(const JournalEntry &entry)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("journal add: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "INSERT INTO journal_entries(id, plant_id, ts_utc, kind, note, ts_edited) "
            "VALUES(:id, :plant, :ts, :kind, COALESCE(:note, ''), :edited)")); // null note -> ''
        q.bindValue(QStringLiteral(":id"), entry.id.toString());
        q.bindValue(QStringLiteral(":plant"),
                    entry.plant ? QVariant(entry.plant->toString()) : QVariant()); // null == global
        q.bindValue(QStringLiteral(":ts"), detail::toIso(entry.timestamp));
        q.bindValue(QStringLiteral(":kind"), static_cast<int>(entry.kind));
        q.bindValue(QStringLiteral(":note"), entry.note);
        q.bindValue(QStringLiteral(":edited"),
                    entry.editedAt ? QVariant(detail::toIso(*entry.editedAt)) : QVariant()); // null == never edited
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("journal_entry"), entry.id.toString(),
                                QStringLiteral("insert"), journalPayload(entry));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("journal add: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqliteJournalRepository::update(const JournalEntry &entry)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("journal update: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "UPDATE journal_entries SET plant_id = :plant, ts_utc = :ts, kind = :kind, "
            "note = COALESCE(:note, ''), ts_edited = :edited WHERE id = :id"));
        q.bindValue(QStringLiteral(":plant"),
                    entry.plant ? QVariant(entry.plant->toString()) : QVariant()); // null == global
        q.bindValue(QStringLiteral(":ts"), detail::toIso(entry.timestamp));
        q.bindValue(QStringLiteral(":kind"), static_cast<int>(entry.kind));
        q.bindValue(QStringLiteral(":note"), entry.note);
        q.bindValue(QStringLiteral(":edited"),
                    entry.editedAt ? QVariant(detail::toIso(*entry.editedAt)) : QVariant()); // null == never edited
        q.bindValue(QStringLiteral(":id"), entry.id.toString());
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("journal_entry"), entry.id.toString(),
                                QStringLiteral("update"), journalPayload(entry));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("journal update: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqliteJournalRepository::remove(JournalEntryId id)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("journal remove: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral("DELETE FROM journal_entries WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), id.toString());
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("journal_entry"), id.toString(),
                                QStringLiteral("delete"),
                                QJsonObject{ { QStringLiteral("id"), id.toString() } });
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("journal remove: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

QList<JournalEntry> SqliteJournalRepository::forPlant(PlantId plant) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, plant_id, ts_utc, kind, note, ts_edited FROM journal_entries "
        "WHERE plant_id = :plant ORDER BY ts_utc DESC, id DESC"));
    q.bindValue(QStringLiteral(":plant"), plant.toString());
    detail::execPreparedOrThrow(q);
    QList<JournalEntry> out;
    while (q.next())
        out.append(entryFromQuery(q));
    return out;
}

QList<JournalEntry> SqliteJournalRepository::globalEntries() const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, plant_id, ts_utc, kind, note, ts_edited FROM journal_entries "
        "WHERE plant_id IS NULL ORDER BY ts_utc DESC, id DESC"));
    detail::execPreparedOrThrow(q);
    QList<JournalEntry> out;
    while (q.next())
        out.append(entryFromQuery(q));
    return out;
}

} // namespace klr
