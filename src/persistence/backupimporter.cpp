// SPDX-License-Identifier: GPL-3.0-or-later
#include "backupimporter.h"

#include "attachment.h"
#include "backupserializer.h" // kFormatVersion (the version we can read)
#include "backuptokens.h"
#include "binding.h"
#include "carestatus.h"
#include "iattachmentrepository.h"
#include "ibindingrepository.h"
#include "icarethresholdrepository.h"
#include "ijournalrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "journalentry.h"
#include "plant.h"
#include "reading.h"
#include "sensor.h"
#include "storageerror.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtCore/QTimeZone>

#include <array>

namespace klr {

namespace {

QDateTime parseIso(const QString &s)
{
    return QDateTime::fromString(s, Qt::ISODate).toUTC();
}

QUuid parseUuid(const QString &s)
{
    return QUuid::fromString(s);
}

std::optional<double> parseValue(const QJsonValue &v)
{
    return v.isNull() || v.isUndefined() ? std::nullopt : std::optional<double>(v.toDouble());
}

} // namespace

BackupImporter::BackupImporter(IPlantRepository &plants, ISensorRepository &sensors,
                               IBindingRepository &bindings, IReadingRepository &readings,
                               IJournalRepository &journal,
                               ICareThresholdRepository &thresholds, const Clock &clock,
                               IAttachmentRepository *attachments)
    : m_plants(plants)
    , m_sensors(sensors)
    , m_bindings(bindings)
    , m_readings(readings)
    , m_journal(journal)
    , m_thresholds(thresholds)
    , m_clock(clock)
    , m_attachments(attachments)
{
}

BackupImporter::Result BackupImporter::importFrom(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        throw StorageError(QStringLiteral("Not a valid backup file: %1").arg(parseError.errorString()));

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("formatVersion"))
        || root.value(QStringLiteral("app")).toString() != QStringLiteral("klorophylle"))
        throw StorageError(QStringLiteral("Not a klorophylle backup file."));

    const int version = root.value(QStringLiteral("formatVersion")).toInt();
    if (version > BackupSerializer::kFormatVersion)
        throw StorageError(QStringLiteral("This backup was written by a newer version of "
                                          "Klorophylle (format %1) and cannot be restored.")
                               .arg(version));

    Result result;

    // Plants — upsert by id (get-or-update), so re-import is a no-op.
    for (const QJsonValue &v : root.value(QStringLiteral("plants")).toArray()) {
        const QJsonObject o = v.toObject();
        Plant p;
        p.id = PlantId{ parseUuid(o.value(QStringLiteral("id")).toString()) };
        p.displayName = o.value(QStringLiteral("displayName")).toString();
        p.species = o.value(QStringLiteral("species")).toString();
        p.trackedSince = parseIso(o.value(QStringLiteral("trackedSince")).toString());
        if (m_plants.get(p.id))
            m_plants.update(p);
        else
            m_plants.add(p);
        ++result.plants;
    }

    // Sensors — id-preserving add() (NOT ensure, which would mint a new id and orphan
    // the bindings/readings keyed on the original SensorId).
    for (const QJsonValue &v : root.value(QStringLiteral("sensors")).toArray()) {
        const QJsonObject o = v.toObject();
        const std::optional<HandleKind> kind =
            backuptokens::fromToken<HandleKind>(o.value(QStringLiteral("handleKind")).toString());
        if (!kind) {
            result.warnings << QStringLiteral("Skipped sensor with unknown handleKind '%1'.")
                                   .arg(o.value(QStringLiteral("handleKind")).toString());
            continue;
        }
        Sensor s;
        s.id = SensorId{ parseUuid(o.value(QStringLiteral("id")).toString()) };
        s.model = o.value(QStringLiteral("model")).toString();
        s.handleKind = *kind;
        s.handleValue = o.value(QStringLiteral("handleValue")).toString();
        s.firstSeen = parseIso(o.value(QStringLiteral("firstSeen")).toString());
        m_sensors.add(s);
        ++result.sensors;
    }

    // Bindings — preserve the [validFrom, validTo) window + role verbatim; dedup on
    // (plant, sensor, validFrom). A closed binding is bind() then unbind().
    for (const QJsonValue &v : root.value(QStringLiteral("bindings")).toArray()) {
        const QJsonObject o = v.toObject();
        const PlantId plant{ parseUuid(o.value(QStringLiteral("plant")).toString()) };
        const SensorId sensor{ parseUuid(o.value(QStringLiteral("sensor")).toString()) };
        const QDateTime validFrom = parseIso(o.value(QStringLiteral("validFrom")).toString());

        std::optional<Quantity> role;
        const QJsonValue roleVal = o.value(QStringLiteral("role"));
        if (!roleVal.isNull() && !roleVal.isUndefined()) {
            role = backuptokens::fromToken<Quantity>(roleVal.toString());
            if (!role) {
                result.warnings << QStringLiteral("Skipped binding with unknown role '%1'.")
                                       .arg(roleVal.toString());
                continue;
            }
        }

        bool dup = false;
        for (const PlantSensorBinding &existing : m_bindings.bindings(plant)) {
            if (existing.sensor == sensor && existing.validFrom == validFrom) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;

        const QJsonValue validToVal = o.value(QStringLiteral("validTo"));
        try {
            m_bindings.bind(plant, sensor, validFrom, role);
            if (!validToVal.isNull() && !validToVal.isUndefined())
                m_bindings.unbind(plant, sensor, parseIso(validToVal.toString()));
            ++result.bindings;
        } catch (const StorageError &e) {
            result.warnings << QStringLiteral("Skipped a binding: %1").arg(QString::fromUtf8(e.what()));
        }
    }

    // Readings — sensor-keyed; the repo dedups per (sensor, quantity, bucket), so an
    // identical sample collapses on re-import.
    for (const QJsonValue &v : root.value(QStringLiteral("readings")).toArray()) {
        const QJsonObject o = v.toObject();
        const std::optional<Quantity> q =
            backuptokens::fromToken<Quantity>(o.value(QStringLiteral("quantity")).toString());
        const std::optional<Provenance> prov =
            backuptokens::fromToken<Provenance>(o.value(QStringLiteral("provenance")).toString());
        if (!q || !prov) {
            result.warnings << QStringLiteral("Skipped a reading with an unknown enum token.");
            continue;
        }
        const SensorId sensor{ parseUuid(o.value(QStringLiteral("sensor")).toString()) };
        Reading r;
        r.quantity = *q;
        r.value = parseValue(o.value(QStringLiteral("value")));
        r.unit = canonicalUnit(*q);
        r.timestamp = parseIso(o.value(QStringLiteral("ts")).toString());
        r.provenance = *prov;
        m_readings.append(sensor, std::array{ r });
        ++result.readings;
    }

    // Journal — upsert by id (no get(id); match within the plant's entries).
    for (const QJsonValue &v : root.value(QStringLiteral("journal")).toArray()) {
        const QJsonObject o = v.toObject();
        const std::optional<JournalEntryKind> kind =
            backuptokens::fromToken<JournalEntryKind>(o.value(QStringLiteral("kind")).toString());
        if (!kind) {
            result.warnings << QStringLiteral("Skipped journal entry with unknown kind '%1'.")
                                   .arg(o.value(QStringLiteral("kind")).toString());
            continue;
        }
        JournalEntry e;
        e.id = JournalEntryId{ parseUuid(o.value(QStringLiteral("id")).toString()) };
        const QJsonValue plant = o.value(QStringLiteral("plant"));
        if (!plant.isUndefined() && !plant.isNull()) // absent -> a global (plant-less) entry (ADR 0022)
            e.plant = PlantId{ parseUuid(plant.toString()) };
        e.timestamp = parseIso(o.value(QStringLiteral("ts")).toString());
        e.kind = *kind;
        e.note = o.value(QStringLiteral("note")).toString();
        const QJsonValue edited = o.value(QStringLiteral("tsEdited"));
        if (!edited.isUndefined() && !edited.isNull()) // absent (older backup) -> never edited
            e.editedAt = parseIso(edited.toString());

        bool exists = false;
        const QList<JournalEntry> siblings =
            e.plant ? m_journal.forPlant(*e.plant) : m_journal.globalEntries();
        for (const JournalEntry &existing : siblings) {
            if (existing.id == e.id) {
                exists = true;
                break;
            }
        }
        if (exists)
            m_journal.update(e);
        else
            m_journal.add(e);
        ++result.journal;
    }

    // Attachments — metadata rows (ADR 0024). id-preserving; dedup by id so re-import is a no-op
    // (the file bytes are not in the backup, so a restored row may point at a missing file). Ignored
    // when no attachment repository is wired.
    if (m_attachments) {
        QSet<QUuid> existing;
        for (const Attachment &a : m_attachments->all())
            existing.insert(a.id.value);
        for (const QJsonValue &v : root.value(QStringLiteral("attachments")).toArray()) {
            const QJsonObject o = v.toObject();
            Attachment a;
            a.id = AttachmentId{ parseUuid(o.value(QStringLiteral("id")).toString()) };
            a.entry = JournalEntryId{ parseUuid(o.value(QStringLiteral("entry")).toString()) };
            a.fileRef = o.value(QStringLiteral("fileRef")).toString();
            a.caption = o.value(QStringLiteral("caption")).toString();
            a.addedAt = parseIso(o.value(QStringLiteral("addedAt")).toString());
            if (existing.contains(a.id.value))
                m_attachments->updateCaption(a.id, a.caption); // already present — refresh caption only
            else
                m_attachments->add(a);
            ++result.attachments;
        }
    }

    // Thresholds — setRange upserts per (plant, quantity).
    for (const QJsonValue &v : root.value(QStringLiteral("thresholds")).toArray()) {
        const QJsonObject o = v.toObject();
        const std::optional<Quantity> q =
            backuptokens::fromToken<Quantity>(o.value(QStringLiteral("quantity")).toString());
        if (!q) {
            result.warnings << QStringLiteral("Skipped threshold with unknown quantity '%1'.")
                                   .arg(o.value(QStringLiteral("quantity")).toString());
            continue;
        }
        const PlantId plant{ parseUuid(o.value(QStringLiteral("plant")).toString()) };
        CareRange range;
        range.quantity = *q;
        range.min = parseValue(o.value(QStringLiteral("min")));
        range.max = parseValue(o.value(QStringLiteral("max")));
        m_thresholds.setRange(plant, range);
        ++result.thresholds;
    }

    return result;
}

} // namespace klr
