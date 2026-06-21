// SPDX-License-Identifier: GPL-3.0-or-later
#include "backupserializer.h"

#include "attachment.h"
#include "backuptokens.h"
#include "binding.h"
#include "carestatus.h"
#include "clock.h"
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

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimeZone>

namespace klr {

namespace {

// All readings ever stored for a sensor fall inside this window — backup dumps
// everything (range windowing is deliberately out of scope, ADR 0010).
const QDateTime kAllFrom(QDate(1970, 1, 1), QTime(0, 0), QTimeZone::UTC);
const QDateTime kAllTo(QDate(9999, 12, 31), QTime(23, 59, 59), QTimeZone::UTC);

QJsonValue isoUtc(const QDateTime &dt)
{
    if (!dt.isValid())
        return QJsonValue(QJsonValue::Null);
    return dt.toUTC().toString(Qt::ISODate);
}

QJsonValue optNum(std::optional<double> v)
{
    return v.has_value() ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
}

} // namespace

BackupSerializer::BackupSerializer(IPlantRepository &plants, ISensorRepository &sensors,
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

QByteArray BackupSerializer::toJson() const
{
    const QList<Plant> plants = m_plants.all();
    const QList<Sensor> sensors = m_sensors.all();

    QJsonArray plantsJson;
    for (const Plant &p : plants) {
        plantsJson.append(QJsonObject{
            { QStringLiteral("id"), p.id.toString() },
            { QStringLiteral("displayName"), p.displayName },
            { QStringLiteral("species"), p.species },
            { QStringLiteral("trackedSince"), isoUtc(p.trackedSince) },
        });
    }

    QJsonArray sensorsJson;
    for (const Sensor &s : sensors) {
        sensorsJson.append(QJsonObject{
            { QStringLiteral("id"), s.id.toString() },
            { QStringLiteral("model"), s.model },
            { QStringLiteral("handleKind"), backuptokens::toToken(s.handleKind) },
            { QStringLiteral("handleValue"), s.handleValue },
            { QStringLiteral("firstSeen"), isoUtc(s.firstSeen) },
        });
    }

    // Bindings: the join between plant and sensor, serialized verbatim (window + role).
    QJsonArray bindingsJson;
    for (const Plant &p : plants) {
        for (const PlantSensorBinding &b : m_bindings.bindings(p.id)) {
            bindingsJson.append(QJsonObject{
                { QStringLiteral("plant"), b.plant.toString() },
                { QStringLiteral("sensor"), b.sensor.toString() },
                { QStringLiteral("validFrom"), isoUtc(b.validFrom) },
                { QStringLiteral("validTo"),
                  b.validTo.has_value() ? isoUtc(*b.validTo) : QJsonValue(QJsonValue::Null) },
                { QStringLiteral("role"),
                  b.role.has_value() ? QJsonValue(backuptokens::toToken(*b.role))
                                     : QJsonValue(QJsonValue::Null) },
            });
        }
    }

    // Readings stay sensor-keyed (as stored); the plant relation is the bindings above.
    QJsonArray readingsJson;
    for (const Sensor &s : sensors) {
        for (int qi = 0; qi < kQuantityCount; ++qi) {
            const auto q = static_cast<Quantity>(qi);
            for (const Reading &r : m_readings.history(s.id, q, kAllFrom, kAllTo)) {
                readingsJson.append(QJsonObject{
                    { QStringLiteral("sensor"), s.id.toString() },
                    { QStringLiteral("quantity"), backuptokens::toToken(q) },
                    { QStringLiteral("value"), optNum(r.value) },
                    { QStringLiteral("ts"), isoUtc(r.timestamp) },
                    { QStringLiteral("provenance"), backuptokens::toToken(r.provenance) },
                });
            }
        }
    }

    const auto journalEntryToJson = [](const JournalEntry &e) {
        QJsonObject jo{
            { QStringLiteral("id"), e.id.toString() },
            { QStringLiteral("ts"), isoUtc(e.timestamp) },
            { QStringLiteral("kind"), backuptokens::toToken(e.kind) },
            { QStringLiteral("note"), e.note },
        };
        // plant is written only for plant-scoped entries; a global entry (ADR 0022) omits it (the
        // importer reads absent -> nullopt, so it round-trips both ways).
        if (e.plant)
            jo.insert(QStringLiteral("plant"), e.plant->toString());
        // tsEdited is written only when set; an older backup simply omits it (importer reads
        // absent -> nullopt, so it round-trips both ways).
        if (e.editedAt)
            jo.insert(QStringLiteral("tsEdited"), isoUtc(*e.editedAt));
        return jo;
    };

    QJsonArray journalJson;
    for (const Plant &p : plants) {
        for (const JournalEntry &e : m_journal.forPlant(p.id))
            journalJson.append(journalEntryToJson(e));
    }
    // Global (plant-less) entries are not reached by the per-plant walk above (ADR 0022).
    for (const JournalEntry &e : m_journal.globalEntries())
        journalJson.append(journalEntryToJson(e));

    // Attachment METADATA rows (ADR 0024 decision 7) — id-preserving so the entry FK round-trips.
    // The file BYTES are deliberately NOT in the backup: a restore on a fresh machine keeps the rows
    // (the startup sweep tolerates a missing file) and the UI shows "image unavailable".
    QJsonArray attachmentsJson;
    if (m_attachments) {
        for (const Attachment &a : m_attachments->all()) {
            attachmentsJson.append(QJsonObject{
                { QStringLiteral("id"), a.id.toString() },
                { QStringLiteral("entry"), a.entry.toString() },
                { QStringLiteral("fileRef"), a.fileRef },
                { QStringLiteral("caption"), a.caption },
                { QStringLiteral("addedAt"), isoUtc(a.addedAt) },
            });
        }
    }

    QJsonArray thresholdsJson;
    for (const Plant &p : plants) {
        for (const CareRange &range : m_thresholds.thresholdsFor(p.id)) {
            thresholdsJson.append(QJsonObject{
                { QStringLiteral("plant"), p.id.toString() },
                { QStringLiteral("quantity"), backuptokens::toToken(range.quantity) },
                { QStringLiteral("min"), optNum(range.min) },
                { QStringLiteral("max"), optNum(range.max) },
            });
        }
    }

    QJsonObject root{
        { QStringLiteral("formatVersion"), kFormatVersion },
        { QStringLiteral("app"), QStringLiteral("klorophylle") },
        { QStringLiteral("exportedAt"),
          isoUtc(QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC)) },
        { QStringLiteral("plants"), plantsJson },
        { QStringLiteral("sensors"), sensorsJson },
        { QStringLiteral("bindings"), bindingsJson },
        { QStringLiteral("readings"), readingsJson },
        { QStringLiteral("journal"), journalJson },
        { QStringLiteral("attachments"), attachmentsJson },
        { QStringLiteral("thresholds"), thresholdsJson },
    };

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace klr
