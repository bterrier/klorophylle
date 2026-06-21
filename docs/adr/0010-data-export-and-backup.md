# ADR 0010 — Data export, backup & restore

**Status:** accepted
**Builds on:** ADR 0001 (UUID identity vocabulary — the round-trip hinges on it), ADR 0005
(plant↔sensor binding window), ADR 0006 (`LegacyImporter` — export is its mirror image; the bucketed
reading upsert that makes restore idempotent).

Today the **Export** route is a wired-up placeholder (`NavigationController::Route::Export` →
`ExportScreen.qml` "coming soon"). This ADR makes it real. Unlike WatchFlower's reading-history CSV
(`src/DeviceManager_export.cpp`: fixed columns, home-root dump, no plants, no round-trip),
klorophylle owns a rich plant-first dataset — plants, sensors, time-bounded bindings,
sensor-keyed readings, journal, care thresholds — so export serves **two distinct jobs**:

1. **Readings CSV** — a human/spreadsheet dump of reading history, plant-facing, in the user's
   display units. *Lossy by design* (no identities, no bindings); for analysis, not restore.
2. **Backup** — a lossless, id-preserving **JSON** snapshot of the whole dataset in canonical units,
   **plus a restore path** that brings it back through the repositories. For moving machines and
   safekeeping ahead of sync.

Both follow the `LegacyImporter` discipline exactly: **the serializers/importer read and write only
through the repositories** (no SQL escapes the boundary), are **pure**
(string/bytes in, string/bytes out — no file IO, the export timestamp is passed in from the injected
`Clock`), and are exercised against the `InMemory*` repos. File IO lives at the GUI edge in
`AppContext`, exactly as legacy's `exportDataSave()` wrapped the pure `exportData()`.

## Decisions

### Readings CSV — tidy/long layout, display units (`klr_persistence`, pure)

`ReadingsCsvExporter` produces a single CSV (`QString`). A **wide** table (one column
per quantity) does not fit the new model: there are 16 `Quantity` values and readings from
different sensors almost never share an exact timestamp, so a wide grid is mostly empty. We adopt a
**tidy/long** layout — **one row per reading**:

```
plant,species,sensor_model,timestamp,quantity,value,unit,provenance
```

- **Plant-facing**, not sensor-facing: iterate `IPlantRepository::all()`; for each plant fetch its
  `IBindingRepository::bindings(plant)`; for each `Quantity` with data call
  `IReadingRepository::seriesForPlant(bindings, q, from, to)`. History therefore follows the plant
  across swaps (ADR 0005) — the CSV shows what the *plant* experienced, which is the app's mental model.
- **Units** come from a `DisplayUnits` value passed in (temp / pressure / illuminance prefs resolved
  from the `SettingsStore` by the caller — the exporter stays pure). Conversion uses the `klr_core`
  `units` helpers; the resolved unit label goes in the `unit` column so mixed quantities are
  unambiguous.
- **Absent values → empty cell** — never the `-99` sentinel.
- **Timestamps**: stored UTC, rendered ISO-8601 in **local time with offset** (`2026-06-09T14:03:00+02:00`)
  — friendly for a spreadsheet yet unambiguous.
- **RFC 4180 quoting** for any field containing a comma/quote/newline (plant display names are
  user-controlled). A pure `csvField(QString)` helper, unit-tested.
- Empty dataset → header line only (never an error).

### Backup JSON — canonical SI, id-preserving (`klr_persistence`, pure)

`BackupSerializer::toJson(...)` returns a `QByteArray`. JSON over a raw SQLite file copy because it is
**decoupled from the SQLite schema version** (a backup taken at schema vN restores into vM through the
repositories + migrations, not a byte-identical file), human-inspectable, and round-trips through the
same boundary `LegacyImporter` uses. Shape:

```jsonc
{
  "formatVersion": 1,
  "app": "klorophylle",
  "exportedAt": "2026-06-09T12:03:00Z",   // injected Clock, UTC
  "plants":     [ { "id": "...", "displayName": "...", "species": "...", "trackedSince": "...Z" } ],
  "sensors":    [ { "id": "...", "model": "...", "handleKind": "Mac", "handleValue": "...", "firstSeen": "...Z" } ],
  "bindings":   [ { "plant": "...", "sensor": "...", "validFrom": "...Z", "validTo": null, "role": "AirTemperature" } ],
  "readings":   [ { "sensor": "...", "quantity": "SoilMoisture", "value": 42.0, "ts": "...Z", "provenance": "History" } ],
  "journal":    [ { "id": "...", "plant": "...", "ts": "...Z", "kind": "Watering", "note": "..." } ],
  "thresholds": [ { "plant": "...", "quantity": "SoilMoisture", "min": 15.0, "max": 60.0 } ]
}
```

- **Every entity carries its app-minted UUID** (ADR 0001). Readings stay **sensor-keyed** (as stored);
  the plant relation is reconstructed from `bindings` on restore — so the binding *is* the join,
  preserved verbatim including its `[validFrom, validTo)` window and optional `role`.
- **Canonical units** — no display conversion; `unit` is omitted because it is derivable from
  `canonicalUnit(quantity)`. `value` is `null` for absent readings.
- **Enums serialize as stable string tokens, never ints.** Int values shift if an enumerator is
  reordered/inserted (and `Quantity` explicitly reserves "keep last"); tokens are reorder-proof and
  legible. A pure bidirectional `backuptokens` table covers `Quantity`, `Unit`, `Provenance`,
  `JournalEntryKind`, `HandleKind` — unit-tested for **total** coverage so adding an enumerator
  without a token fails the test.
- `QDateTime` as ISO-8601 UTC; `std::optional` / open binding → `null`.

### Restore — `BackupImporter` mirroring `LegacyImporter` (`klr_persistence`)

`BackupImporter::importFrom(json)` parses, validates, and writes **through the repositories** (change-log
+ FK discipline hold), returning a `LegacyImporter`-style `Result { plants, sensors, bindings, readings,
journal, thresholds, warnings }`.

- **Upsert by UUID** so restore is idempotent (re-importing the same backup is a no-op):
  plants/journal `get(id) ? update : add`; readings `append()` dedup on `(sensor, quantity, ts_bucket)`
  in the repo (ADR 0006) so duplicate samples collapse; bindings dedup on `(plant, sensor, validFrom)`.
- **Sensors keep their original UUID.** `ISensorRepository::ensure(handle,…)` mints a *new* id (it
  dedups by handle for the live BLE path), which would orphan the backup's bindings/readings keyed on
  the old `SensorId`. So restore needs an **id-preserving insert** — a new
  `ISensorRepository::add(const Sensor&)` (insert-with-id, get-or-update by id), parallel to the
  plant repo's existing `add(const Plant&)`. `ensure()` stays the live path. *(This is the one
  repository-interface addition in this ADR.)*
- **Forward-compatible:** an unknown enum token → a `warning` and that row is **skipped** (not fatal);
  a `formatVersion` newer than supported → **refuse** with a clear `StorageError` ("backup written by a
  newer version").

### Where files go, and how restore picks one

- **Export** writes to a fixed, discoverable folder — `DocumentsLocation/Klorophylle/` — with
  `Clock`-stamped names `klorophylle_<ts>.csv` / `klorophylle_<ts>.json` (no wall-clock reads). After a
  successful write, **reveal** the folder via `QDesktopServices::openUrl(folderUrl)`.
- **Restore** needs a file the app didn't write, so it reuses the **existing native file-picker
  pattern** already serving legacy import (`AppContext::importLegacyDatabase(fileUrl)` + a starting-folder
  helper). A `backupImportFolder()` helper points the picker at the export folder.

### `AppContext` surface (mirrors the import surface)

```cpp
Q_INVOKABLE QString exportReadingsCsv();   // writes file, returns path ("" on failure)
Q_INVOKABLE QStringList exportPeriodLabels() const; // CSV window options (see addendum)
Q_INVOKABLE QString exportBackup();        //  "
Q_INVOKABLE void    restoreBackup(const QString &fileUrl);
Q_INVOKABLE void    revealExportFolder();
Q_INVOKABLE QString exportFolder() const;
Q_INVOKABLE QString backupImportFolder() const;
signals:
    void exportFinished(const QString &summary, bool ok, const QString &folderUrl);
    void restoreFinished(const QString &summary, bool ok);
```

The three helpers (`ReadingsCsvExporter`, `BackupSerializer`, `BackupImporter`) are constructed in
`app/main.cpp` with the repo refs + `Clock` and injected as pointers — exactly how `LegacyImporter`
and `PlantDuplicator` are wired today. Restore refreshes the plant list + selected-plant care (as
`importLegacyDatabase` already does).

## Test matrix

| Area | Test | Asserts |
|------|------|---------|
| pure | `backuptokens` | every enumerator of all five enums has a token and round-trips (total coverage — guards new enumerators) |
| pure | CSV layout | tidy rows from seeded in-mem repos; °C→°F conversion; absent→empty; RFC-4180 quoting of a comma'd plant name; empty dataset → header only |
| pure | CSV attribution | a sensor swap re-homes history to the plant active at each sample (ADR 0005) |
| repo | `ISensorRepository::add` | insert-with-id preserves UUID; re-add updates; parity in-mem == SQLite |
| serde | backup **round-trip** | serialize in-mem repos → JSON → import into fresh in-mem repos → all entities equal incl. UUIDs, binding windows, optional/null fields *(headline test)* |
| serde | restore **idempotency** | importing the same backup twice → no duplicate plants/sensors/bindings/readings |
| serde | forward-compat | unknown enum token → warning + skipped row; `formatVersion`+1 → refused with clear error |
| gui | `AppContext` | export writes a file under the export folder and emits `exportFinished(ok=true)`; restore wiring refreshes the plant/care models; CSV row count tracks the selected export period (addendum) |

## Implementation order (one commit each)

1. **persistence**: `backuptokens.{h,cpp}` (enum↔token, all five enums) + `test_backuptokens`.
2. **persistence**: `ISensorRepository::add(const Sensor&)` in both impls + parity in `test_repository`.
3. **persistence**: `ReadingsCsvExporter` (pure, `DisplayUnits` + range in, `QString` out) + `test_readingscsvexport`.
4. **persistence**: `BackupSerializer::toJson` (canonical, id-preserving) + `test_backupserialize`.
5. **persistence**: `BackupImporter::importFrom` (upsert, forward-compat) + round-trip / idempotency / version tests.
6. **gui**: `AppContext` export/backup/restore invokables + folder/reveal helpers + signals; wire the three helpers in `app/main.cpp`.
7. **gui**: `ExportScreen.qml` — two action cards ("Export readings (CSV)" / "Back up all data"), a result line + "Show in folder", and a "Restore from backup" file-picker reusing the import-dialog pattern.

## Deliberately out of scope (this feature)

- **Export range / retention windowing** — *(superseded — see addendum; now shipped.)* originally
  deferred: export everything, a range knob is a later settings option (legacy had `getExportRangeDays`).
- **Journal photos** — planned for the journal but not yet in the model; the backup schema gains a
  `photo` field once they land (`formatVersion` bump), and the CSV ignores them. See the
  journal-photo plan.
- **Selective / per-plant export**, **scheduled/automatic backups**, **cloud sync**.
- **CSV *import*** — the only import paths are the legacy `data.db` (ADR 0006) and JSON restore.

## Files

New under `src/persistence/`: `backuptokens.{h,cpp}`, `readingscsvexporter.{h,cpp}`,
`backupserializer.{h,cpp}`, `backupimporter.{h,cpp}`. Changed: `isensorrepository.h` +
`inmemorysensorrepository.{h,cpp}` + `sqlitesensorrepository.{h,cpp}` (`add`), `gui/appcontext.{h,cpp}`,
`app/main.cpp`, `gui/qml/ExportScreen.qml`. Tests: `test_backuptokens`, `test_readingscsvexport`,
`test_backupserialize`, `test_backupimport`, extended `test_repository`.

## Addendum — CSV export period

The "export range" item above was deferred but then ported from legacy, since
`ReadingsCsvExporter::exportCsv(units, from, to)` was already range-aware — only the UI and
the `from`/`to` choice were missing. The legacy `getExportRangeDays` UX is reproduced:

- **`AppContext`** owns the canonical period table — *All data / Last 24 hours / 7 / 30 / 90 days* —
  and exposes `exportPeriodLabels()` for the dropdown. `exportReadingsCsv()` maps the chosen index
  to a day count, computes the lower bound from the injected `Clock` (open-ended upper bound; "All
  data" or a missing clock → epoch), and passes it to the unchanged exporter.
- **`SettingsStore`** persists only the chosen *index* (`exportPeriodIndex`, 0 = "All data") via
  `IKeyValueStore`, mirroring `colorScheme`. `kExportPeriodCount` tracks the table size.
- **QML** stays thin: a `ComboBox` bound to `exportPeriodLabels()` writing back `Settings.exportPeriodIndex`.

Tests: `test_settingsstore` (default / persist / clamp) and `test_appcontext`
(`csvExportWindowFollowsSelectedPeriod` — row count tracks the period). The exporter and
repositories were untouched.
