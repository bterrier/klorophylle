// SPDX-License-Identifier: GPL-3.0-or-later
#include "schema.h"
#include "sqlsupport.h"

namespace klr {

std::vector<Migration> baselineMigrations()
{
    std::vector<Migration> migrations;

    migrations.push_back({ 1, "baseline", [](QSqlQuery &q) {
        // Per-install metadata (replica_id for the change-log, ...).
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE app_meta ("
            "  key   TEXT PRIMARY KEY,"
            "  value TEXT NOT NULL)"));

        // Plants — first-class, sensor-less allowed (plant-first, goal #1).
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE plants ("
            "  id            TEXT PRIMARY KEY,"          // UUIDv7
            "  display_name  TEXT NOT NULL,"
            "  species       TEXT NOT NULL DEFAULT '',"  // freeform for now
            "  tracked_since TEXT NOT NULL)"));          // UTC ISO-8601

        // Care journal — entries cascade-delete with their plant.
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE journal_entries ("
            "  id       TEXT PRIMARY KEY,"               // UUIDv7
            "  plant_id TEXT NOT NULL REFERENCES plants(id) ON DELETE CASCADE,"
            "  ts_utc   TEXT NOT NULL,"                  // UTC ISO-8601
            "  kind     INTEGER NOT NULL,"               // JournalEntryKind
            "  note     TEXT NOT NULL DEFAULT '')"));
        detail::execOrThrow(q, QStringLiteral(
            "CREATE INDEX idx_journal_plant_ts ON journal_entries(plant_id, ts_utc)"));

        // Sync-ready substrate: an append-only, transactional change-log. The HLC
        // reducer + transport are deferred; this only
        // guarantees the log exists and every mutation writes to it atomically.
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE change_log ("
            "  seq         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  entity      TEXT NOT NULL,"
            "  entity_id   TEXT NOT NULL,"
            "  op          TEXT NOT NULL,"               // insert | update | delete
            "  ts_utc      TEXT NOT NULL,"
            "  hlc_ms      INTEGER NOT NULL,"
            "  hlc_counter INTEGER NOT NULL DEFAULT 0,"
            "  replica_id  TEXT NOT NULL,"
            "  payload_json TEXT)"));
    } });

    // v2 — the plant<->sensor join (ADR 0005). Readings key on sensor_id; the
    // plant they belong to is derived through the time-bounded binding window, so a
    // shared sensor's sample is stored once and attributes to every bound plant.
    migrations.push_back({ 2, "plant-sensor-binding", [](QSqlQuery &q) {
        // Physical sensors — plant-agnostic. The app-minted SensorId is the stable
        // sync/binding key; the raw platform handle is stored separately and dedup
        // matches on it, never assuming a MAC.
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE sensors ("
            "  id           TEXT PRIMARY KEY,"           // UUIDv7 SensorId
            "  model        TEXT NOT NULL DEFAULT '',"
            "  handle_kind  INTEGER NOT NULL,"           // HandleKind: 0=Mac, 1=CoreBluetoothUuid
            "  handle_value TEXT NOT NULL,"              // raw MAC / CoreBluetooth UUID
            "  first_seen   TEXT NOT NULL,"
            "  UNIQUE(handle_kind, handle_value))"));    // dedup on the handle, never the id

        // Time-bounded plant<->sensor edges. Many-to-many BOTH ways: NO
        // UNIQUE(plant_id, sensor_id) — a plant has many sensors, a sensor serves many
        // plants (shared pot), and a pair recurs over time (swap = close + open).
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE plant_sensor_bindings ("
            "  id         TEXT PRIMARY KEY,"             // UUIDv7 (its own syncable identity)
            "  plant_id   TEXT NOT NULL REFERENCES plants(id)  ON DELETE CASCADE,"
            "  sensor_id  TEXT NOT NULL REFERENCES sensors(id) ON DELETE CASCADE,"
            "  valid_from TEXT NOT NULL,"
            "  valid_to   TEXT,"                         // NULL == currently bound
            "  role       INTEGER)"));                   // Quantity, NULL == supplies all it measures
        detail::execOrThrow(q, QStringLiteral(
            "CREATE INDEX idx_binding_plant ON plant_sensor_bindings(plant_id, valid_from)"));
        detail::execOrThrow(q, QStringLiteral(
            "CREATE INDEX idx_binding_sensor ON plant_sensor_bindings(sensor_id, valid_from)"));

        // Readings key on sensor_id (never plant_id). The bucketing + write-cadence
        // gate and ON CONFLICT replace-vs-keep policy come with the history work; this
        // lands the table + a plain append so binding-attribution can be proven now.
        // ts_bucket/source/observed_by exist from day one so that later work fills them
        // without a migration.
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE readings ("
            "  sensor_id   TEXT NOT NULL REFERENCES sensors(id) ON DELETE CASCADE,"
            "  quantity    INTEGER NOT NULL,"            // Quantity
            "  ts_utc      TEXT NOT NULL,"
            "  ts_bucket   TEXT NOT NULL,"               // write-cadence / bucketing key
            "  value       REAL,"                        // NULL == absent, never -99
            "  source      INTEGER NOT NULL,"            // Provenance
            "  observed_by TEXT NOT NULL DEFAULT '',"    // node that saw it (for multi-node sync)
            "  PRIMARY KEY(sensor_id, quantity, ts_bucket))"));
    } });

    // v3 — per-plant care thresholds (ADR 0009). The single mutable owner of "what
    // this plant alerts on" (the "active" ranges): seeded from the
    // catalog species' ideal ranges when a species is chosen, then overridable. The
    // catalog stays the immutable owner of *ideal* ranges; the sensor stores no limits.
    migrations.push_back({ 3, "care-thresholds", [](QSqlQuery &q) {
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE care_thresholds ("
            "  plant_id   TEXT NOT NULL REFERENCES plants(id) ON DELETE CASCADE,"
            "  quantity   INTEGER NOT NULL,"             // Quantity
            "  min_value  REAL,"                         // NULL == no lower bound
            "  max_value  REAL,"                         // NULL == no upper bound
            "  PRIMARY KEY(plant_id, quantity))"));      // one ideal range per quantity
    } });

    // v4 — per-sensor history-sync bookkeeping (ADR 0014). Records when THIS replica last
    // completed a GATT history download for a sensor, so the next launch fetches only newer entries
    // and a (battery-draining) connection happens at most once per cadence. Device-LOCAL: NOT
    // change-logged — each replica tracks its own connects, so it must never sync across devices.
    migrations.push_back({ 4, "sensor-sync-state", [](QSqlQuery &q) {
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE sensor_sync_state ("
            "  sensor_id          TEXT PRIMARY KEY REFERENCES sensors(id) ON DELETE CASCADE,"
            "  last_history_sync  TEXT)"));         // ISO-8601 UTC; row absent == never synced
    } });

    // v5 — AI-agent transcripts (ADR 0019). Conversations and their ordered messages.
    // A message body is stored OPAQUE (role int + content_json) — the karness::Message <-> JSON
    // mapping lives in klr_agent, the layer that may see karness (klr_persistence sits below it).
    // Device-LOCAL like sensor_sync_state: NOT change-logged, so a transcript never syncs.
    migrations.push_back({ 5, "agent-transcripts", [](QSqlQuery &q) {
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE agent_conversations ("
            "  id          TEXT PRIMARY KEY,"        // UUIDv7 ConversationId
            "  created_at  TEXT NOT NULL,"           // UTC ISO-8601
            "  title       TEXT NOT NULL DEFAULT '')"));

        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE agent_messages ("
            "  id              TEXT PRIMARY KEY,"     // UUIDv7 AgentMessageId
            "  conversation_id TEXT NOT NULL REFERENCES agent_conversations(id) ON DELETE CASCADE,"
            "  seq             INTEGER NOT NULL,"     // ordering within the conversation (0-based)
            "  role            INTEGER NOT NULL,"     // karness::Role projection (opaque here)
            "  content_json    TEXT NOT NULL,"        // serialized karness::Message blocks
            "  created_at      TEXT NOT NULL,"        // UTC ISO-8601
            "  UNIQUE(conversation_id, seq))"));
        detail::execOrThrow(q, QStringLiteral(
            "CREATE INDEX idx_agent_msg_conv ON agent_messages(conversation_id, seq)"));
    } });

    // v6 — dual-timestamp journal entries (ADR 0020). A second, NULLABLE timestamp recording when an
    // entry was last edited (NULL == never edited; the no-sentinel rule). The existing ts_utc stays the
    // entry date and the sort key — ts_edited is NOT a sort key. Pre-existing rows migrate to NULL: we
    // have no record they were ever edited, so "never edited" is the honest default.
    migrations.push_back({ 6, "journal-edited-timestamp", [](QSqlQuery &q) {
        detail::execOrThrow(q, QStringLiteral(
            "ALTER TABLE journal_entries ADD COLUMN ts_edited TEXT")); // UTC ISO-8601, nullable
    } });

    // v7 — global (plant-less) journal entries (ADR 0022). plant_id becomes NULLABLE so an entry can
    // belong to no plant: the global journal that holds user-wide agent memory + global notes. SQLite
    // can't drop a NOT NULL constraint in place, so the table is rebuilt (the standard 12-step ALTER):
    // create the relaxed table, copy every row (incl. the v6 ts_edited), drop the old, rename, and
    // recreate the index. The FK to plants(id) ON DELETE CASCADE is kept — a NULL FK is never
    // cascaded, so a global entry survives a plant deletion. No table references journal_entries, so
    // the drop/rename has no inbound FK to worry about.
    migrations.push_back({ 7, "journal-optional-plant", [](QSqlQuery &q) {
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE journal_entries_new ("
            "  id        TEXT PRIMARY KEY,"               // UUIDv7
            "  plant_id  TEXT REFERENCES plants(id) ON DELETE CASCADE," // NULL == global entry
            "  ts_utc    TEXT NOT NULL,"                  // UTC ISO-8601 (entry date / sort key)
            "  kind      INTEGER NOT NULL,"               // JournalEntryKind
            "  note      TEXT NOT NULL DEFAULT '',"
            "  ts_edited TEXT)"));                        // UTC ISO-8601, nullable (ADR 0020)
        detail::execOrThrow(q, QStringLiteral(
            "INSERT INTO journal_entries_new(id, plant_id, ts_utc, kind, note, ts_edited) "
            "SELECT id, plant_id, ts_utc, kind, note, ts_edited FROM journal_entries"));
        detail::execOrThrow(q, QStringLiteral("DROP TABLE journal_entries"));
        detail::execOrThrow(q, QStringLiteral(
            "ALTER TABLE journal_entries_new RENAME TO journal_entries"));
        detail::execOrThrow(q, QStringLiteral(
            "CREATE INDEX idx_journal_plant_ts ON journal_entries(plant_id, ts_utc)"));
    } });

    // v8 — journal photo attachments (ADR 0024). Zero-or-many per entry, cascade-deleting with it; a
    // photo is an evidence axis on ANY entry, never a JournalEntryKind. file_ref is an app-data-relative
    // path to the file on disk (never a BLOB — keeps the DB small + portable); only metadata lives here.
    // Syncable like every entity: UUIDv7 PK + a change_log row per mutation (written by the repository).
    migrations.push_back({ 8, "journal-attachments", [](QSqlQuery &q) {
        detail::execOrThrow(q, QStringLiteral(
            "CREATE TABLE attachments ("
            "  id        TEXT PRIMARY KEY,"             // UUIDv7 AttachmentId
            "  entry_id  TEXT NOT NULL REFERENCES journal_entries(id) ON DELETE CASCADE,"
            "  file_ref  TEXT NOT NULL,"                // app-data-relative path ("attachments/<uuid>.<ext>")
            "  caption   TEXT NOT NULL DEFAULT '',"     // free text — "Before"/"After", etc.
            "  added_at  TEXT NOT NULL)"));             // UTC ISO-8601
        detail::execOrThrow(q, QStringLiteral(
            "CREATE INDEX idx_attachment_entry ON attachments(entry_id, added_at)"));
    } });

    return migrations;
}

} // namespace klr
