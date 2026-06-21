// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqliteattachmentrepository.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtCore/QJsonObject>
#include <QtCore/QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

namespace {

QJsonObject attachmentPayload(const Attachment &a)
{
    return QJsonObject{
        { QStringLiteral("id"), a.id.toString() },
        { QStringLiteral("entry"), a.entry.toString() },
        { QStringLiteral("fileRef"), a.fileRef },
        { QStringLiteral("caption"), a.caption },
        { QStringLiteral("addedAt"), detail::toIso(a.addedAt) },
    };
}

Attachment attachmentFromQuery(const QSqlQuery &q)
{
    Attachment a;
    a.id = AttachmentId{ QUuid::fromString(q.value(QStringLiteral("id")).toString()) };
    a.entry = JournalEntryId{ QUuid::fromString(q.value(QStringLiteral("entry_id")).toString()) };
    a.fileRef = q.value(QStringLiteral("file_ref")).toString();
    a.caption = q.value(QStringLiteral("caption")).toString();
    a.addedAt = detail::fromIso(q.value(QStringLiteral("added_at")).toString());
    return a;
}

} // namespace

void SqliteAttachmentRepository::add(const Attachment &attachment)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("attachment add: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "INSERT INTO attachments(id, entry_id, file_ref, caption, added_at) "
            "VALUES(:id, :entry, :ref, COALESCE(:caption, ''), :added)"));
        q.bindValue(QStringLiteral(":id"), attachment.id.toString());
        q.bindValue(QStringLiteral(":entry"), attachment.entry.toString());
        q.bindValue(QStringLiteral(":ref"), attachment.fileRef);
        q.bindValue(QStringLiteral(":caption"), attachment.caption);
        q.bindValue(QStringLiteral(":added"), detail::toIso(attachment.addedAt));
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("attachment"), attachment.id.toString(),
                                QStringLiteral("insert"), attachmentPayload(attachment));
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("attachment add: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqliteAttachmentRepository::updateCaption(AttachmentId id, const QString &caption)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("attachment caption: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral(
            "UPDATE attachments SET caption = COALESCE(:caption, '') WHERE id = :id"));
        q.bindValue(QStringLiteral(":caption"), caption);
        q.bindValue(QStringLiteral(":id"), id.toString());
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("attachment"), id.toString(),
                                QStringLiteral("update"),
                                QJsonObject{ { QStringLiteral("id"), id.toString() },
                                             { QStringLiteral("caption"), caption } });
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("attachment caption: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

void SqliteAttachmentRepository::remove(AttachmentId id)
{
    QSqlDatabase d = m_db.handle();
    if (!d.transaction())
        throw StorageError(QStringLiteral("attachment remove: begin failed: %1").arg(d.lastError().text()));
    try {
        QSqlQuery q(d);
        detail::prepareOrThrow(q, QStringLiteral("DELETE FROM attachments WHERE id = :id"));
        q.bindValue(QStringLiteral(":id"), id.toString());
        detail::execPreparedOrThrow(q);

        detail::appendChangeLog(m_db, QStringLiteral("attachment"), id.toString(),
                                QStringLiteral("delete"),
                                QJsonObject{ { QStringLiteral("id"), id.toString() } });
        if (!d.commit()) {
            const QString err = d.lastError().text();
            d.rollback();
            throw StorageError(QStringLiteral("attachment remove: commit failed: %1").arg(err));
        }
    } catch (...) {
        d.rollback();
        throw;
    }
}

QList<Attachment> SqliteAttachmentRepository::forEntry(JournalEntryId entry) const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, entry_id, file_ref, caption, added_at FROM attachments "
        "WHERE entry_id = :entry ORDER BY added_at ASC, id ASC"));
    q.bindValue(QStringLiteral(":entry"), entry.toString());
    detail::execPreparedOrThrow(q);
    QList<Attachment> out;
    while (q.next())
        out.append(attachmentFromQuery(q));
    return out;
}

QList<Attachment> SqliteAttachmentRepository::all() const
{
    QSqlDatabase d = m_db.handle();
    QSqlQuery q(d);
    detail::prepareOrThrow(q, QStringLiteral(
        "SELECT id, entry_id, file_ref, caption, added_at FROM attachments "
        "ORDER BY added_at ASC, id ASC"));
    detail::execPreparedOrThrow(q);
    QList<Attachment> out;
    while (q.next())
        out.append(attachmentFromQuery(q));
    return out;
}

} // namespace klr
