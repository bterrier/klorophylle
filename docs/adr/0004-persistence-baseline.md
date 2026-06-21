# ADR 0004 — Persistence baseline (SQLite)

**Status:** accepted (Milestone B)

The first on-disk storage lands with the plant-first domain (plants + journal). It is the
substrate every later phase (history, probe, sync, AI) sits on, so the shape is chosen to be
forward-compatible without building the machinery yet.

## Decisions

- **`klr_persistence` is the only layer that includes `<QtSql>`.** Domain value types
  (`Plant`, `JournalEntry`), repository interfaces, in-memory fakes, and the `Sqlite*Repository`
  impls all live here; SQL never leaks above it. No separate `klr_domain` library yet — split it
  out when a second consumer (UI view-models / probe) forces it.
- **Single connection per process** (one named `QSqlDatabase` owned by `Database`). The per-thread
  `ConnectionPool` is **deferred** until the headless probe actually shares the file.
  `Database` holds only the connection *name*, never a long-lived `QSqlDatabase` copy, so
  `removeDatabase()` never warns.
- **PRAGMAs on open:** `foreign_keys = ON`, `journal_mode = WAL`, `synchronous = NORMAL`, plus
  `QSQLITE_BUSY_TIMEOUT` as a connect option. WAL + busy_timeout is the GUI-and-probe sharing model.
- **Fail-loud transactional migrations** (`MigrationRunner`): each step runs inside
  `transaction()`, every `exec()` is checked, and a failure rolls back and **throws `StorageError`**;
  `schema_version` only advances after a clean commit. No silent `DROP`. This directly addresses the
  half-applied-migration failure mode WatchFlower could hit.
- **Sync-ready baseline schema (v1).** Every syncable entity has a **UUIDv7 `TEXT` primary key**
  (ADR 0001). A transactional **`change_log`** table exists from day one and every
  mutation writes exactly one row to it *in the same transaction* (`entity`, `entity_id`, `op`,
  `ts_utc`, `hlc_ms`, `hlc_counter`, `replica_id`, `payload_json`). The HLC reducer, conflict
  resolution, and transport are **deferred** — this layer only guarantees the log
  exists and is atomic. `replica_id` is a per-install UUIDv7 minted on first use into `app_meta`.
- **Injected `Clock`** (`klr_core`) stamps the change-log time; tests pass `FakeClock`.
- **`std::optional` / `NULL`, never sentinels** — carried over from the value types.

## Deliberately out of scope (this step)

- **No reading time-series persistence.** A sensor reading is a *utility*; the first-class thing to
  persist is the plant + journal. `IReadingRepository` stays in-memory-only, and the
  `SensorId`-vs-`PlantId` reading-keying question is deferred to when readings are
  actually stored against a plant (the binding/history phases).
- No `care_thresholds` table, species/FTS5 catalog, legacy importer, or UI — each lands with
  the phase that needs it.

## Files

`src/persistence/`: `database.{h,cpp}`, `migrationrunner.{h,cpp}`, `schema.{h,cpp}`,
`sqlsupport.{h,cpp}`, `storageerror.h`, `plant.h`, `journalentry.h`, the `I*Repository` interfaces,
the `InMemory*` fakes, and the `Sqlite{Plant,Journal}Repository` impls. Covered by
`tests/test_{plantrepository,journalrepository,migration,changelog}` (the SQLite impls run the same
behavioural suite as the in-memory fakes).
