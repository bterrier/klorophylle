// SPDX-License-Identifier: GPL-3.0-or-later
#include "agenttools.h"

#include "attachment.h"
#include "backuptokens.h" // toToken/fromToken<JournalEntryKind>
#include "binding.h"      // PlantSensorBinding
#include "carestatus.h"   // evaluate, rangeFor, CareStatus, CareRange
#include "clock.h"
#include "format.h"       // label, formatValue
#include "iattachmentfilestore.h"
#include "iattachmentrepository.h"
#include "ibindingrepository.h"
#include "icarethresholdrepository.h"
#include "ijournalrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "iwebfetcher.h"
#include "journalentry.h"
#include "plant.h"
#include "reading.h" // Quantity, canonicalUnit, Reading
#include "webcontent.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QTimeZone>
#include <QtCore/QUuid>
#include <algorithm>
#include <expected>
#include <optional>
#include <utility>

namespace klr {

namespace {

using karness::ImageBlock;
using karness::TextBlock;
using karness::ToolOutcome;
using karness::ToolSpec;

// The image MIME type for a stored attachment, derived from its file extension.
QString mimeForRef(const QString &fileRef)
{
    const QString lower = fileRef.toLower();
    if (lower.endsWith(QStringLiteral(".png")))
        return QStringLiteral("image/png");
    if (lower.endsWith(QStringLiteral(".webp")))
        return QStringLiteral("image/webp");
    if (lower.endsWith(QStringLiteral(".gif")))
        return QStringLiteral("image/gif");
    return QStringLiteral("image/jpeg"); // jpg/jpeg and the default
}

QFuture<ToolOutcome> ready(ToolOutcome outcome)
{
    return QtFuture::makeReadyValueFuture(std::move(outcome));
}

ToolOutcome okText(const QString &text)
{
    return ToolOutcome{{TextBlock{text}}, false};
}

// An isError outcome — re-injected into the conversation so the model can recover
// (never a turn failure; itool.h / decision 8).
ToolOutcome errText(const QString &text)
{
    return ToolOutcome{{TextBlock{text}}, true};
}

// Resolve the required "plant_id" argument to a Plant, or an isError outcome explaining
// why (missing/blank arg, malformed id, or no such plant). Shared by every plant-addressed
// tool — argument validation is the tool's job (itool.h ships no schema validator).
std::expected<Plant, ToolOutcome> resolvePlant(const QJsonObject &args, const IPlantRepository &plants)
{
    const QJsonValue v = args.value(QStringLiteral("plant_id"));
    if (!v.isString() || v.toString().isEmpty())
        return std::unexpected(errText(QStringLiteral("missing required string argument 'plant_id'")));
    const QString s = v.toString();
    const QUuid uuid = QUuid::fromString(s);
    if (uuid.isNull())
        return std::unexpected(errText(QStringLiteral("'%1' is not a valid plant id").arg(s)));
    const std::optional<Plant> plant = plants.get(PlantId{uuid});
    if (!plant)
        return std::unexpected(errText(QStringLiteral("no plant is tracked with id '%1'").arg(s)));
    return *plant;
}

// A UTC ISO-8601 timestamp for the agent (deterministic, locale-free).
QString isoUtc(const QDateTime &t)
{
    return t.toUTC().toString(Qt::ISODate);
}

// A JSON-Schema object with the given properties; `required` names the mandatory ones.
QJsonObject objectSchema(const QJsonObject &properties, const QStringList &required = {})
{
    QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("additionalProperties"), false}};
    if (!required.isEmpty()) {
        QJsonArray req;
        for (const QString &r : required)
            req.append(r);
        schema.insert(QStringLiteral("required"), req);
    }
    return schema;
}

// Whole days elapsed since `since` at `nowMs`, floored, never negative; nullopt when
// `since` is invalid.
std::optional<qint64> daysTracked(const QDateTime &since, qint64 nowMs)
{
    if (!since.isValid())
        return std::nullopt;
    const qint64 elapsed = nowMs - since.toMSecsSinceEpoch();
    if (elapsed <= 0)
        return 0;
    return elapsed / (24LL * 60 * 60 * 1000);
}

QString plural(qint64 n, const QString &unit)
{
    return QString::number(n) + QLatin1Char(' ') + unit + (n == 1 ? QString() : QStringLiteral("s"));
}

// The single Memory entry in a newest-first list, or end() if none. Both the per-plant and the
// global memory tools keep exactly one Memory entry per scope; if several somehow exist, the
// newest (first) wins.
QList<JournalEntry>::const_iterator findMemory(const QList<JournalEntry> &entries)
{
    return std::find_if(entries.begin(), entries.end(), [](const JournalEntry &e) {
        return e.kind == JournalEntryKind::Memory;
    });
}

// Stable order shared by the roster-facing tools: oldest tracked first, id tie-break.
void sortPlants(QList<Plant> &plants)
{
    std::sort(plants.begin(), plants.end(), [](const Plant &a, const Plant &b) {
        if (a.trackedSince != b.trackedSince)
            return a.trackedSince < b.trackedSince;
        return a.id.toString() < b.id.toString();
    });
}

} // namespace

// --- list_plants -------------------------------------------------------------------------

ListPlantsTool::ListPlantsTool(const IPlantRepository &plants, const Clock &clock)
    : m_plants(plants)
    , m_clock(clock)
{
}

ToolSpec ListPlantsTool::spec() const
{
    return ToolSpec{
        QStringLiteral("list_plants"),
        QStringLiteral("List every plant the user is tracking, with its id (use the id to "
                       "call other tools), display name, species, and how many days it has "
                       "been tracked. Takes no arguments."),
        objectSchema({})};
}

QFuture<ToolOutcome> ListPlantsTool::invoke(const QJsonObject &)
{
    QList<Plant> plants = m_plants.all();
    sortPlants(plants);

    if (plants.isEmpty())
        return ready(okText(QStringLiteral("No plants are being tracked yet.")));

    const qint64 nowMs = m_clock.nowMs();
    QString out = QStringLiteral("%1:\n").arg(plural(plants.size(), QStringLiteral("plant")));
    for (const Plant &p : plants) {
        const QString name = p.displayName.isEmpty() ? QStringLiteral("(unnamed)") : p.displayName;
        const QString species =
            p.species.isEmpty() ? QStringLiteral("species unknown") : QStringLiteral("species ") + p.species;
        const std::optional<qint64> days = daysTracked(p.trackedSince, nowMs);
        const QString tracked =
            days ? QStringLiteral("tracked ") + plural(*days, QStringLiteral("day"))
                 : QStringLiteral("tracked since unknown");
        out += QStringLiteral("- %1 — %2 — %3 — id %4\n").arg(name, species, tracked, p.id.toString());
    }
    return ready(okText(out));
}

// --- read_plant_journal ------------------------------------------------------------------

namespace {
constexpr int kDefaultJournalLimit = 10;
} // namespace

ReadPlantJournalTool::ReadPlantJournalTool(const IPlantRepository &plants,
                                           const IJournalRepository &journal)
    : m_plants(plants)
    , m_journal(journal)
{
}

ToolSpec ReadPlantJournalTool::spec() const
{
    const QJsonObject properties{
        {QStringLiteral("plant_id"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("Plant id from list_plants.")}}},
        {QStringLiteral("limit"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                     {QStringLiteral("description"),
                      QStringLiteral("Maximum number of most-recent entries to return "
                                     "(default 10).")}}}};
    return ToolSpec{
        QStringLiteral("read_plant_journal"),
        QStringLiteral("Read a plant's care-journal entries (waterings, notes, observations, …), "
                       "most recent first."),
        objectSchema(properties, {QStringLiteral("plant_id")})};
}

QFuture<ToolOutcome> ReadPlantJournalTool::invoke(const QJsonObject &args)
{
    const std::expected<Plant, ToolOutcome> plant = resolvePlant(args, m_plants);
    if (!plant)
        return ready(plant.error());

    int limit = kDefaultJournalLimit;
    const QJsonValue limitArg = args.value(QStringLiteral("limit"));
    if (limitArg.isDouble() && limitArg.toInt() > 0)
        limit = limitArg.toInt();

    const QList<JournalEntry> all = m_journal.forPlant(plant->id); // newest-first
    const QString name = plant->displayName.isEmpty() ? QStringLiteral("(unnamed)") : plant->displayName;

    if (all.isEmpty())
        return ready(okText(QStringLiteral("%1 has no journal entries.").arg(name)));

    const int shown = std::min(limit, static_cast<int>(all.size()));
    QString out = QStringLiteral("%1 — %2 of %3 journal entries (newest first):\n")
                      .arg(name).arg(shown).arg(all.size());
    for (int i = 0; i < shown; ++i) {
        const JournalEntry &e = all.at(i);
        const QString note = e.note.isEmpty() ? QStringLiteral("(no note)") : e.note;
        out += QStringLiteral("- %1 — %2 — %3\n")
                   .arg(isoUtc(e.timestamp), backuptokens::toToken(e.kind), note);
    }
    return ready(okText(out));
}

// --- read_plant_data ---------------------------------------------------------------------

namespace {

constexpr qint64 kMsPerDay = 24LL * 60 * 60 * 1000;

// Format a bare value in the canonical unit of `q`, reusing formatValue's precision/symbol.
QString valueText(double v, Quantity q)
{
    return formatValue(Reading{q, v, canonicalUnit(q), {}, {}});
}

// The ideal range as text: "18.0–28.0 °C", "≥ 18.0 %", "≤ 1000.0 µS/cm".
QString rangeText(const CareRange &r, Quantity q)
{
    if (r.min && r.max)
        return QStringLiteral("%1–%2").arg(valueText(*r.min, q), valueText(*r.max, q));
    if (r.min)
        return QStringLiteral("≥ %1").arg(valueText(*r.min, q));
    return QStringLiteral("≤ %1").arg(valueText(*r.max, q));
}

// The care verdict + ideal range for one current reading, e.g. "(above ideal; ideal 18.0–28.0 °C)"
// or "(no threshold set)".
QString statusText(const Reading &reading, std::span<const CareRange> ranges)
{
    const std::optional<CareRange> range = rangeFor(ranges, reading.quantity);
    if (!range || !range->isSet())
        return QStringLiteral("(no threshold set)");
    const QString ideal = rangeText(*range, reading.quantity);
    switch (evaluate(reading.value, *range)) {
    case CareStatus::Ideal:   return QStringLiteral("(ideal; range %1)").arg(ideal);
    case CareStatus::TooLow:  return QStringLiteral("(below ideal; range %1)").arg(ideal);
    case CareStatus::TooHigh: return QStringLiteral("(above ideal; range %1)").arg(ideal);
    case CareStatus::Unknown: return QStringLiteral("(no threshold set)");
    }
    return QString();
}

} // namespace

ReadPlantDataTool::ReadPlantDataTool(const IPlantRepository &plants, const IBindingRepository &bindings,
                                     const IReadingRepository &readings,
                                     const ICareThresholdRepository &thresholds, const Clock &clock)
    : m_plants(plants)
    , m_bindings(bindings)
    , m_readings(readings)
    , m_thresholds(thresholds)
    , m_clock(clock)
{
}

ToolSpec ReadPlantDataTool::spec() const
{
    const QJsonObject properties{
        {QStringLiteral("plant_id"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("Plant id from list_plants.")}}},
        {QStringLiteral("window_days"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                     {QStringLiteral("description"),
                      QStringLiteral("If set, also summarise each quantity's min/avg/max over "
                                     "the last this-many days.")}}}};
    return ToolSpec{
        QStringLiteral("read_plant_data"),
        QStringLiteral("Read a plant's current sensor readings (soil moisture, temperature, "
                       "light, …), each judged against its ideal range, with an optional recent "
                       "min/avg/max history window."),
        objectSchema(properties, {QStringLiteral("plant_id")})};
}

QFuture<ToolOutcome> ReadPlantDataTool::invoke(const QJsonObject &args)
{
    const std::expected<Plant, ToolOutcome> plant = resolvePlant(args, m_plants);
    if (!plant)
        return ready(plant.error());

    const QString name = plant->displayName.isEmpty() ? QStringLiteral("(unnamed)") : plant->displayName;
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);

    const QList<PlantSensorBinding> active = m_bindings.activeFor(plant->id, now);
    QList<Reading> current = m_readings.currentForPlant(active);
    if (current.isEmpty())
        return ready(okText(QStringLiteral("%1 has no current sensor readings (no bound sensor has "
                                           "reported a value).").arg(name)));

    // Deterministic order: by Quantity enum ordinal.
    std::sort(current.begin(), current.end(),
              [](const Reading &a, const Reading &b) { return a.quantity < b.quantity; });

    const QList<CareRange> ranges = m_thresholds.thresholdsFor(plant->id);

    QString out = QStringLiteral("%1 — current readings:\n").arg(name);
    for (const Reading &r : current)
        out += QStringLiteral("- %1: %2 %3\n").arg(label(r.quantity), formatValue(r), statusText(r, ranges));

    // Optional history window.
    const QJsonValue windowArg = args.value(QStringLiteral("window_days"));
    if (windowArg.isDouble() && windowArg.toInt() > 0) {
        const int days = windowArg.toInt();
        // Every binding (open or closed), so the series follows the plant across swaps.
        const QList<PlantSensorBinding> allBindings = m_bindings.bindings(plant->id);
        const QDateTime from = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs() - days * kMsPerDay,
                                                              QTimeZone::UTC);
        QString windowOut;
        for (const Reading &r : current) {
            const QList<Reading> series = m_readings.seriesForPlant(allBindings, r.quantity, from, now);
            double sum = 0;
            int count = 0;
            for (const Reading &s : series)
                if (s.value) {
                    sum += *s.value;
                    ++count;
                }
            if (count == 0)
                continue;
            const Extremes ex = extremesOf(series);
            windowOut += QStringLiteral("- %1: min %2, avg %3, max %4\n")
                             .arg(label(r.quantity), valueText(*ex.min, r.quantity),
                                  valueText(sum / count, r.quantity), valueText(*ex.max, r.quantity));
        }
        if (!windowOut.isEmpty())
            out += QStringLiteral("\nLast %1:\n").arg(plural(days, QStringLiteral("day"))) + windowOut;
    }
    return ready(okText(out));
}

// --- add_journal_entry -------------------------------------------------------------------

namespace {

// The journal-kind tokens, in enum order — the schema enum and the "valid kinds" error.
QStringList journalKindTokens()
{
    QStringList tokens;
    for (int i = static_cast<int>(JournalEntryKind::Note);
         i <= static_cast<int>(JournalEntryKind::Observation); ++i)
        tokens.append(backuptokens::toToken(static_cast<JournalEntryKind>(i)));
    return tokens;
}

} // namespace

AddJournalEntryTool::AddJournalEntryTool(const IPlantRepository &plants, IJournalRepository &journal,
                                         const Clock &clock)
    : m_plants(plants)
    , m_journal(journal)
    , m_clock(clock)
{
}

ToolSpec AddJournalEntryTool::spec() const
{
    QJsonArray kindEnum;
    for (const QString &t : journalKindTokens())
        kindEnum.append(t);
    const QJsonObject properties{
        {QStringLiteral("plant_id"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("Plant id from list_plants.")}}},
        {QStringLiteral("note"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("The entry text.")}}},
        {QStringLiteral("kind"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("enum"), kindEnum},
                     {QStringLiteral("description"),
                      QStringLiteral("Entry type (default Note).")}}}};
    return ToolSpec{
        QStringLiteral("add_journal_entry"),
        QStringLiteral("Add an entry to a plant's care journal (e.g. record a watering or a note). "
                       "This writes to the user's data."),
        objectSchema(properties, {QStringLiteral("plant_id"), QStringLiteral("note")})};
}

QFuture<ToolOutcome> AddJournalEntryTool::invoke(const QJsonObject &args)
{
    const std::expected<Plant, ToolOutcome> plant = resolvePlant(args, m_plants);
    if (!plant)
        return ready(plant.error());

    const QJsonValue noteArg = args.value(QStringLiteral("note"));
    if (!noteArg.isString() || noteArg.toString().isEmpty())
        return ready(errText(QStringLiteral("missing required non-empty string argument 'note'")));

    JournalEntryKind kind = JournalEntryKind::Note;
    const QJsonValue kindArg = args.value(QStringLiteral("kind"));
    if (!kindArg.isUndefined() && !kindArg.isNull()) {
        const std::optional<JournalEntryKind> parsed =
            backuptokens::fromToken<JournalEntryKind>(kindArg.toString());
        if (!parsed)
            return ready(errText(QStringLiteral("unknown kind '%1'; valid kinds are: %2")
                                     .arg(kindArg.toString(), journalKindTokens().join(QStringLiteral(", ")))));
        kind = *parsed;
    }

    const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
    m_journal.add(JournalEntry{JournalEntryId::generate(), plant->id, now, kind, noteArg.toString()});

    const QString name = plant->displayName.isEmpty() ? QStringLiteral("(unnamed)") : plant->displayName;
    return ready(okText(QStringLiteral("Added a %1 entry to %2's journal at %3.")
                            .arg(backuptokens::toToken(kind), name, isoUtc(now))));
}

// --- set_plant_memory --------------------------------------------------------------------

SetPlantMemoryTool::SetPlantMemoryTool(const IPlantRepository &plants, IJournalRepository &journal,
                                       const Clock &clock)
    : m_plants(plants)
    , m_journal(journal)
    , m_clock(clock)
{
}

ToolSpec SetPlantMemoryTool::spec() const
{
    const QJsonObject properties{
        {QStringLiteral("plant_id"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("Plant id from list_plants.")}}},
        {QStringLiteral("text"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"),
                      QStringLiteral("The COMPLETE memory for this plant — durable facts to carry "
                                     "across conversations. Read the existing memory first, then "
                                     "pass the whole rewritten text (it replaces the old memory).")}}}};
    return ToolSpec{
        QStringLiteral("set_plant_memory"),
        QStringLiteral("Save durable memory about a plant (facts to remember across conversations, "
                       "e.g. 'owner waters lightly', 'south-facing window'). Replaces this plant's "
                       "memory with the full text you pass; it is visible and editable by the user."),
        objectSchema(properties, {QStringLiteral("plant_id"), QStringLiteral("text")})};
}

QFuture<ToolOutcome> SetPlantMemoryTool::invoke(const QJsonObject &args)
{
    const std::expected<Plant, ToolOutcome> plant = resolvePlant(args, m_plants);
    if (!plant)
        return ready(plant.error());

    const QJsonValue textArg = args.value(QStringLiteral("text"));
    if (!textArg.isString() || textArg.toString().isEmpty())
        return ready(errText(QStringLiteral("missing required non-empty string argument 'text'")));
    const QString text = textArg.toString();

    const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
    const QString name = plant->displayName.isEmpty() ? QStringLiteral("(unnamed)") : plant->displayName;

    // forPlant() is newest-first, so the first Memory entry is the one to keep if several exist.
    const QList<JournalEntry> all = m_journal.forPlant(plant->id);
    const auto existing = findMemory(all);

    if (existing != all.end()) {
        // In-place rewrite: keep the stable id, replace the text, bump BOTH dates to float it to now.
        JournalEntry entry = *existing;
        entry.note = text;
        entry.timestamp = now;
        entry.editedAt = now;
        m_journal.update(entry);
        return ready(okText(QStringLiteral("Updated %1's memory at %2.").arg(name, isoUtc(now))));
    }

    // First memory for this plant: a creation, so editedAt stays nullopt (the fresh entry date floats it).
    m_journal.add(JournalEntry{JournalEntryId::generate(), plant->id, now, JournalEntryKind::Memory, text});
    return ready(okText(QStringLiteral("Saved %1's memory at %2.").arg(name, isoUtc(now))));
}

// --- set_global_memory -------------------------------------------------------------------

SetGlobalMemoryTool::SetGlobalMemoryTool(IJournalRepository &journal, const Clock &clock)
    : m_journal(journal)
    , m_clock(clock)
{
}

ToolSpec SetGlobalMemoryTool::spec() const
{
    const QJsonObject properties{
        {QStringLiteral("text"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"),
                      QStringLiteral("The COMPLETE user-wide memory — durable facts that apply across "
                                     "all plants. Read the existing memory first, then pass the whole "
                                     "rewritten text (it replaces the old memory).")}}}};
    return ToolSpec{
        QStringLiteral("set_global_memory"),
        QStringLiteral("Save durable user-wide memory (facts that apply to every plant, e.g. 'owner "
                       "travels often', 'hard tap water', 'dry climate'). Replaces the global memory "
                       "with the full text you pass; it is visible and editable by the user."),
        objectSchema(properties, {QStringLiteral("text")})};
}

QFuture<ToolOutcome> SetGlobalMemoryTool::invoke(const QJsonObject &args)
{
    const QJsonValue textArg = args.value(QStringLiteral("text"));
    if (!textArg.isString() || textArg.toString().isEmpty())
        return ready(errText(QStringLiteral("missing required non-empty string argument 'text'")));
    const QString text = textArg.toString();

    const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);

    // globalEntries() is newest-first, so the first Memory entry is the one to keep if several exist.
    const QList<JournalEntry> all = m_journal.globalEntries();
    const auto existing = findMemory(all);

    if (existing != all.end()) {
        // In-place rewrite: keep the stable id, replace the text, bump BOTH dates to float it to now.
        JournalEntry entry = *existing;
        entry.note = text;
        entry.timestamp = now;
        entry.editedAt = now;
        m_journal.update(entry);
        return ready(okText(QStringLiteral("Updated global memory at %1.").arg(isoUtc(now))));
    }

    // First global memory: a creation (plant nullopt, editedAt nullopt — the fresh entry date floats it).
    m_journal.add(JournalEntry{JournalEntryId::generate(), std::nullopt, now, JournalEntryKind::Memory, text});
    return ready(okText(QStringLiteral("Saved global memory at %1.").arg(isoUtc(now))));
}

// --- read_global_memory ------------------------------------------------------------------

ReadGlobalMemoryTool::ReadGlobalMemoryTool(const IJournalRepository &journal)
    : m_journal(journal)
{
}

ToolSpec ReadGlobalMemoryTool::spec() const
{
    return ToolSpec{
        QStringLiteral("read_global_memory"),
        QStringLiteral("Read the durable user-wide memory (facts that apply to every plant). Consult "
                       "it before advising. Takes no arguments."),
        objectSchema({})};
}

QFuture<ToolOutcome> ReadGlobalMemoryTool::invoke(const QJsonObject &)
{
    const QList<JournalEntry> all = m_journal.globalEntries();
    const auto memory = findMemory(all);
    if (memory == all.end())
        return ready(okText(QStringLiteral("No global memory saved yet.")));
    return ready(okText(QStringLiteral("Global memory:\n%1").arg(memory->note)));
}

// --- read_plant_photo --------------------------------------------------------------------

ReadPlantPhotoTool::ReadPlantPhotoTool(const IPlantRepository &plants,
                                       const IJournalRepository &journal,
                                       const IAttachmentRepository &attachments,
                                       const IAttachmentFileStore &fileStore)
    : m_plants(plants)
    , m_journal(journal)
    , m_attachments(attachments)
    , m_fileStore(fileStore)
{
}

ToolSpec ReadPlantPhotoTool::spec() const
{
    const QJsonObject properties{
        {QStringLiteral("plant_id"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("Plant id from list_plants.")}}},
        {QStringLiteral("limit"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                     {QStringLiteral("description"),
                      QStringLiteral("Maximum number of most-recent photos to return (default 4).")}}}};
    return ToolSpec{
        QStringLiteral("read_plant_photo"),
        QStringLiteral("Look at the plant's most recent journal photos (newest first). Use this to "
                       "diagnose problems you can see — leaf colour, spots, wilting, pests."),
        objectSchema(properties, {QStringLiteral("plant_id")})};
}

QFuture<ToolOutcome> ReadPlantPhotoTool::invoke(const QJsonObject &args)
{
    const std::expected<Plant, ToolOutcome> plant = resolvePlant(args, m_plants);
    if (!plant)
        return ready(plant.error());

    int limit = 4;
    const QJsonValue limitArg = args.value(QStringLiteral("limit"));
    if (limitArg.isDouble() && limitArg.toInt() > 0)
        limit = limitArg.toInt();

    // Walk the plant's entries newest-first; within an entry, attachments are oldest-first. Stop once
    // `limit` photos with a readable file have been gathered (a missing file is skipped, not an error).
    QList<karness::ContentPart> parts;
    int shown = 0;
    for (const JournalEntry &entry : m_journal.forPlant(plant->id)) {
        if (shown >= limit)
            break;
        for (const Attachment &a : m_attachments.forEntry(entry.id)) {
            if (shown >= limit)
                break;
            const std::optional<QByteArray> bytes = m_fileStore.read(a.fileRef);
            if (!bytes)
                continue; // file absent (e.g. restored-without-files backup)
            QString caption = a.caption.isEmpty() ? QString() : QStringLiteral(" — %1").arg(a.caption);
            parts.append(TextBlock{QStringLiteral("Photo from %1%2:")
                                       .arg(isoUtc(entry.timestamp), caption)});
            parts.append(ImageBlock{*bytes, mimeForRef(a.fileRef)});
            ++shown;
        }
    }

    if (shown == 0)
        return ready(okText(QStringLiteral("'%1' has no journal photos.").arg(plant->displayName)));
    return ready(ToolOutcome{parts, false});
}

// --- read_online_plant_db ----------------------------------------------------------------

ReadOnlinePlantDbTool::ReadOnlinePlantDbTool(IWebFetcher &fetcher)
    : m_fetcher(fetcher)
{
}

ToolSpec ReadOnlinePlantDbTool::spec() const
{
    const QJsonObject properties{
        {QStringLiteral("query"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"),
                      QStringLiteral("The plant's species / botanical name to look up (e.g. "
                                     "'Aloe vera'). Use the species from the plant roster.")}}},
        {QStringLiteral("source"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("enum"),
                      QJsonArray{QStringLiteral("wikipedia"), QStringLiteral("wikispecies")}},
                     {QStringLiteral("description"),
                      QStringLiteral("Which encyclopedia to read (default 'wikipedia').")}}}};
    return ToolSpec{
        QStringLiteral("read_online_plant_db"),
        QStringLiteral("Look up a plant species in a reputable online encyclopedia (Wikipedia or "
                       "Wikispecies) and return the article as plain text. Use it for background or "
                       "care information you don't already have. The page may not exist for every "
                       "species — if so, say what you found."),
        objectSchema(properties, {QStringLiteral("query")})};
}

QFuture<ToolOutcome> ReadOnlinePlantDbTool::invoke(const QJsonObject &args)
{
    const QJsonValue queryArg = args.value(QStringLiteral("query"));
    if (!queryArg.isString() || queryArg.toString().trimmed().isEmpty())
        return ready(errText(QStringLiteral("missing required non-empty string argument 'query'")));
    const QString query = queryArg.toString();

    webcontent::Source source = webcontent::Source::Wikipedia;
    const QJsonValue sourceArg = args.value(QStringLiteral("source"));
    if (!sourceArg.isUndefined() && !sourceArg.isNull()) {
        const std::optional<webcontent::Source> parsed =
            webcontent::sourceFromToken(sourceArg.toString());
        if (!parsed)
            return ready(errText(QStringLiteral("unknown source '%1'; valid sources are: wikipedia, "
                                                "wikispecies")
                                     .arg(sourceArg.toString())));
        source = *parsed;
    }

    const std::optional<QUrl> url = webcontent::sourceUrl(source, query);
    if (!url || !webcontent::isAllowedHost(*url)) // both hold by construction; guard regardless
        return ready(errText(QStringLiteral("could not build a valid lookup URL for that query")));

    // The only async tool: map the fetch result through htmlToText + truncation off the loop.
    return m_fetcher.fetch(*url).then([](const WebFetchResult &result) -> ToolOutcome {
        if (result.error)
            return errText(QStringLiteral("could not read the page: %1").arg(*result.error));
        if (!result.body)
            return errText(QStringLiteral("the page returned no content"));

        const QString text = webcontent::htmlToText(QString::fromUtf8(*result.body));
        if (text.isEmpty())
            return errText(QStringLiteral("the page had no readable text"));

        QString reduced = text;
        if (reduced.size() > webcontent::kTextBudget)
            reduced = reduced.left(webcontent::kTextBudget) + QStringLiteral("\n[truncated]");

        return okText(QStringLiteral("From %1:\n\n%2").arg(result.finalUrl.host(), reduced));
    });
}

} // namespace klr
