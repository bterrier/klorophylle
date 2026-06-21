# ADR 0020 — Dual-timestamp journal entries (entry date + edited date)

**Status:** accepted
**Builds on:** ADR 0004 (persistence baseline / `journal_entries` v1), ADR 0010 (backup/restore),
ADR 0002 (QML-singleton injection).

A journal entry today carries **one** timestamp (`JournalEntry::timestamp` ⇄ `journal_entries.ts_utc`),
set at creation and never touched again — `update()` rewrites the note/kind but leaves the timestamp,
and the journal sorts by it (`ORDER BY ts_utc DESC`). That conflates two distinct facts the product
now needs to tell apart: **when the entry was made** (its place in the timeline) and **when it was
last changed**. This is a general journal improvement — a user wants to see that they tweaked a note —
**and** it is the foundation the per-plant agent-memory milestone (ADR 0019; ADR 0021) builds on: a `Memory` entry the
agent rewrites should float to "now" while a user's edit of any entry must *not* reorder it. Both fall
out of one change: give every entry a second timestamp.

This ADR lands **only the dual-timestamp model** (data + repository + edit policy + surfacing). The
`Memory` kind and the agent-write "bump both dates" float stay in the agent-memory milestone; this step is AI-agnostic.

## Decisions

1. **`JournalEntry` gains `std::optional<QDateTime> editedAt`; `timestamp` is *the entry date*.**
   The existing `timestamp` field is the **entry date** — the creation instant and the sort key —
   and stays immutable after creation (renaming it is deliberately avoided; it is documented, not
   renamed, to keep the change small). The new `editedAt` is the **last-edited instant**, and it is
   **`std::optional`**: `nullopt` means *never edited* (the project's no-sentinel rule —
   `nullopt` ≠ "edited at the creation time"). UTC, like `timestamp`.

2. **Schema v6: a nullable `ts_edited` column; existing rows migrate to `NULL`.**
   `ALTER TABLE journal_entries ADD COLUMN ts_edited TEXT` (UTC ISO-8601, nullable). Bump
   `kSchemaVersion` 5 → 6 with one migration step `"journal-edited-timestamp"` following the
   `MigrationRunner` pattern (transactional, fail-loud, version bumped only on commit). Pre-existing
   entries get `NULL` — we have no record that they were ever edited, so "never edited" is the honest
   default. The column name keeps the table's own `ts_` convention (matching `ts_utc`), superseding
   ADR 0019's illustrative `edited_at`. **Sort is unchanged** — still `ORDER BY ts_utc DESC, id DESC`
   (by entry date); `ts_edited` is not a sort key.

3. **The repository stays a dumb store — it persists both timestamps verbatim.** No clock in
   `klr_persistence` (the repository boundary reads no wall clock). `SqliteJournalRepository` binds
   `ts_edited` (NULL when `nullopt`) on `add`/`update` and reads it back (NULL → `nullopt`);
   `InMemoryJournalRepository` mirrors it by struct copy. `add()` writes `nullopt` (a fresh entry is
   never-edited); `update()` writes whatever `editedAt` the caller supplies — the *policy* of what
   that value should be lives one layer up (decision 4). Parity-tested SQLite ↔ in-memory: round-trip
   of `editedAt` (absent and present), and that `update` can set it while `timestamp` survives.

4. **The edit policy lives at the business edge (`AppContext`), where the "now" already comes from.**
   `addJournalEntry` already stamps `QDateTime::currentDateTimeUtc()` at the `AppContext` edge; the
   edit path follows suit. A new `Q_INVOKABLE editJournalEntry(QString entryId, int kind, QString note)`
   loads the selected plant's entry by id (`forPlant` + find — no new repo method), **keeps its
   `timestamp`**, sets **`editedAt = currentDateTimeUtc()`**, applies the new kind/note, calls
   `update()`, and refreshes. This is the contract in one sentence: **a user edit moves only the edit
   date; the entry date (and thus the timeline position) is untouched.** Covered by `test_appcontext`.
   *(The agent-memory milestone will add the agent-write path that bumps **both** — out of scope here, but the optional field
   + verbatim-persist repo already support it with no further schema work.)*

5. **Surfacing: a model role + a formatted label; "edited …" shows only when edited.**
   `PlantJournalModel` gains an `EditedAtRole` returning a localized short date-time when `editedAt`
   is set and an **empty string** when `nullopt` (so a never-edited entry shows no edit line). The
   journal entry delegate renders "edited &lt;date&gt;" when the role is non-empty and grows an **edit
   affordance** wired to `editJournalEntry`; QML stays presentation-only (qmllint/cachegen-covered,
   like the rest of the UI). No new label logic in QML — the timestamp is formatted in C++, matching
   `TimestampRole`.

6. **Backup/restore round-trips the new field; older backups import cleanly.** `BackupSerializer`
   writes an optional `tsEdited` on each journal object (omitted/`null` when `nullopt`);
   `BackupImporter` reads it (absent → `nullopt`). A backup taken before this change has no `tsEdited`
   and restores as never-edited — backward compatible. Round-trip proven by `test_backupserialize` /
   `test_backupimport` (byte-identical re-serialization still holds with the field present and
   absent). The **`LegacyImporter`** needs no change: legacy `data.db` entries carry no edit date, so
   they import as `nullopt` (the struct default). `PlantDuplicator` copies entries by value, so a
   clone preserves the original's `editedAt` verbatim — confirm in its test.

## Consequences

- Every journal entry records when it was last changed (`nullopt` = never), distinct from when it was
  made. The journal still orders by entry date, so curating an old note never yanks it to the top.
- The data model is now ready for the agent-memory milestone: an agent `Memory`-entry rewrite that bumps **both** dates will
  float that one entry to "now" with no further schema change — the optional second timestamp plus the
  verbatim-persisting repository are exactly what that needs.
- One sync nuance (no reducer yet): an edit is a normal `journal`/`update` change-log row keyed on
  the entry's stable id; `editedAt` rides along as a field. Last-write-wins on the entry stays
  coherent because the id never churns.
- Backups gain one optional field; the format stays forward/backward compatible.

## Implementation order (one commit each, `ctest` green at each)

1. `JournalEntry::editedAt` (`std::optional<QDateTime>`, documented `timestamp` = entry date) + schema
   v6 migration (`ts_edited` nullable) + `kSchemaVersion` 5 → 6 (+ a migration test: a v5 DB advances
   to v6 with the column added, existing rows `NULL`).
2. `SqliteJournalRepository` + `InMemoryJournalRepository` read/write `editedAt`; extend
   `test_journalrepository` parity (round-trip absent/present; `update` sets `editedAt`, preserves
   `timestamp`).
3. `AppContext::editJournalEntry` (keep `timestamp`, set `editedAt = now`, update + refresh) +
   `test_appcontext` (edit preserves entry date, sets edit date, changes note/kind; never reorders).
4. `PlantJournalModel::EditedAtRole` + formatted label + the journal delegate's "edited …" line and
   edit affordance (qmllint/cachegen).
5. `BackupSerializer`/`BackupImporter` carry optional `tsEdited`; extend `test_backupserialize` /
   `test_backupimport` (round-trip with/without); confirm `PlantDuplicator` clones it
   (`test_plantduplicator`).
6. This ADR + flip ADR 0019's "dual-timestamp prerequisite (its own ADR)" reference to this one (and,
   in passing, note the klorophylle `Memory` kind value is **6**, the next free `JournalEntryKind`).

## Tests

`test_migrationrunner` (v5 → v6 adds `ts_edited`, rows `NULL`), `test_journalrepository`
(`editedAt` round-trip both flavours; `update` semantics), `test_appcontext` (`editJournalEntry`
keeps entry date / sets edit date / no reorder), `test_backupserialize` + `test_backupimport`
(optional field round-trip, old-backup compatibility), `test_plantduplicator` (clone preserves
`editedAt`). The journal QML delegate (edit affordance + "edited …" line) stays qmllint/cachegen-only
by design.

## Out of scope / follow-ups

- The **`Memory` kind** and the **agent-write "bump both dates" float** — the agent-memory milestone (ADR 0021).
- **User-editable *entry* (event) date / backdating** — the parked follow-up in ADR 0019 ("keep it
  simple": the entry date is the creation instant for now).
- A standalone **journal browse screen** beyond the plant-detail journal — not required here; the
  model role + edit seam are ready for whatever renders it.
