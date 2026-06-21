# ADR 0005 — Plant↔sensor binding, multi-sensor & swap

**Status:** accepted · **Supersedes:** the `PlantId`-keyed
`IReadingRepository` sketch deferred in ADR 0004.

This is the **convergence phase** that joins the two halves built so far — the live device layer
(scan → decode → readings) and the plant-first domain (plants + journal). It introduces the
time-bounded **`PlantSensorBinding`** edge and the defining guarantee of goal #1: **history follows
the plant, not the device**. The plant↔sensor relation is **many-to-many in both directions**: a
plant may hold several sensors (multiple probes in one pot), *and* a sensor may serve several plants
(two plants sharing one pot, read by a single probe). A gardener binds and swaps sensors over time —
and a single history query for the plant spans every sensor it ever had, attributed by the binding
window that was open when each sample was taken.

This ADR fixes the entities, the schema (v2), the repository boundary, the **pure** resolution /
aggregation rules, and the test matrix, so implementation lands as a sequence of small,
independently-tested commits rather than one convergence dump.

## Decisions

### Entities (pure value types — `klr_core` / `klr_persistence`)

- **`Sensor`** — `{ SensorId id; QString model; HandleKind handleKind; QString handleValue;
  QDateTime firstSeen; }`. The app-minted **`SensorId`** (ADR 0001, already exists) is the stable
  sync/binding key; the **raw platform handle is stored separately** (`handle_kind` ∈ {`Mac`,
  `CoreBluetoothUuid`}, `handle_value`) so dedup matches on the handle and never assumes a MAC.
  A `Sensor` is plant-agnostic — it carries no plant, no thresholds.
- **`PlantSensorBinding`** — a `Q_GADGET` value edge:
  `{ PlantId plant; SensorId sensor; QDateTime validFrom; std::optional<QDateTime> validTo;
  std::optional<Quantity> role; }`. `validTo == nullopt` means *currently bound*. The edge is
  **many-to-many**: a plant may hold **N concurrent** bindings *and* a sensor may be bound to **N
  plants** at once (two plants in one pot, one shared probe). `role` optionally restricts which
  quantity a sensor supplies for that plant (e.g. pin a thermo-hygrometer to `AirTemperature` only).
- **Swap = close + open**, audited: `unbind` sets `validTo = now` on the open binding; `bind` opens
  a new one with `validFrom = now`. Nothing is deleted, so the swap is reconstructible.

### Resolution & aggregation (pure, clock-injected, unit-tested — `klr_core`)

These are **pure free functions** with literal inputs — no DB, BLE, or QML — per the
minimize-QML/JS and testability commitments.

- `activeBindings(std::span<const PlantSensorBinding>, QDateTime at) -> QList<PlantSensorBinding>`
  — a binding is active at `at` iff `validFrom <= at && (!validTo || at < validTo)`. Half-open at the
  upper bound so an instantaneous swap attributes a sample to exactly one binding.
- **Overlap validation** `validateBinding(existing, candidate) -> std::expected<void, BindingError>`,
  where `existing` is **the candidate plant's own bindings** — the rule is scoped *per plant*, never
  across the sensor's other plants (a probe shared by plants A and B, each pinning it to
  `AirTemperature`, is **not** a conflict — different plants). Within one plant: **redundant no-role
  bindings for the same quantity are allowed** (two soil probes in one big pot); **two *explicit-role*
  bindings for the same quantity with overlapping windows are rejected** (`BindingError::RoleConflict`);
  a no-role + explicit-role overlap is allowed (the role one wins in aggregation). This rule settles
  the domain-model open question.
- **Aggregation** `aggregate(std::span<const Reading>, AggregationPolicy) -> std::optional<Reading>`
  resolves **one** value per `Quantity` from the active sensors' freshest samples:
  1. prefer the sample from a binding with an explicit `role` for that quantity;
  2. else apply the policy across the freshest sample *per sensor* within a freshness window.
  **`AggregationPolicy { NewestWins, Average }` exists from day one; only `NewestWins` is implemented
  this phase.** `Average` (mean of freshest-per-sensor within the window — the same
  monotonic-window idea as `AdvSnapshotAccumulator`) is a documented TODO that lands behind a setting
  in a later phase; the unimplemented branch falls back to `NewestWins` and is marked
  `// TODO: averaging behind a setting`.

### Persistence (schema **v2** — `klr_persistence` only)

`kSchemaVersion` → **2**, one new migration appended to `baselineMigrations()` (transactional,
fail-loud, no `DROP`; same discipline as ADR 0004). New tables:

```sql
CREATE TABLE sensors (
  id           TEXT PRIMARY KEY,            -- app-minted SensorId (UUIDv7)
  model        TEXT NOT NULL DEFAULT '',
  handle_kind  INTEGER NOT NULL,            -- HandleKind: 0=Mac, 1=CoreBluetoothUuid
  handle_value TEXT NOT NULL,               -- raw MAC / CoreBluetooth UUID
  first_seen   TEXT NOT NULL,
  UNIQUE(handle_kind, handle_value));       -- dedup on the handle, never the SensorId

-- Many-to-many: no UNIQUE on (plant_id, sensor_id) — a plant has many sensors,
-- a sensor serves many plants (shared pot), and a pair recurs over time (swap).
CREATE TABLE plant_sensor_bindings (
  id         TEXT PRIMARY KEY,              -- UUIDv7 (its own syncable identity)
  plant_id   TEXT NOT NULL REFERENCES plants(id)  ON DELETE CASCADE,
  sensor_id  TEXT NOT NULL REFERENCES sensors(id) ON DELETE CASCADE,
  valid_from TEXT NOT NULL,
  valid_to   TEXT,                          -- NULL == currently bound
  role       INTEGER);                      -- Quantity, NULL == supplies all it measures
CREATE INDEX idx_binding_plant ON plant_sensor_bindings(plant_id, valid_from);
CREATE INDEX idx_binding_sensor ON plant_sensor_bindings(sensor_id, valid_from);

CREATE TABLE readings (
  sensor_id  TEXT NOT NULL REFERENCES sensors(id) ON DELETE CASCADE,
  quantity   INTEGER NOT NULL,              -- Quantity
  ts_utc     TEXT NOT NULL,
  ts_bucket  TEXT NOT NULL,                 -- write-cadence / bucketing key (one injected Clock)
  value      REAL,                          -- NULL == absent, never -99
  source     INTEGER NOT NULL,              -- Provenance
  observed_by TEXT NOT NULL DEFAULT '',     -- node that saw it (probe/sync substrate, goal #4/#5)
  PRIMARY KEY(sensor_id, quantity, ts_bucket));
```

- **Readings key on `sensor_id`, never `plant_id`.** A reading belongs to a *sensor*; its plant(s)
  are **derived through the binding window** at query time. This is the structural change this phase makes
  over the ADR-0004 sketch (which keyed readings directly on `PlantId` and faked a swap by
  re-appending). The many-to-many relation makes this decisive, not just tidy: a sample from a
  **shared** sensor is stored **once** and attributes to **every** plant bound during its window — a
  `PlantId` key would have to duplicate the row per plant and could never stay consistent.
- The full bucketing + write-cadence gate (one injected `Clock`, `ON CONFLICT` replace-vs-keep) is
  later work; this phase lands the table and a plain append so binding-attribution can be proven now. The
  `ts_bucket`/`source`/`observed_by` columns exist from day one so a later phase fills them without a migration.

### Repository boundary (interfaces in `klr_persistence`; SQL only in the `Sqlite*` impls)

- **`ISensorRepository`**: `ensure(HandleKind, handleValue, model) -> SensorId` (dedup on handle —
  returns the existing id or mints one), `get(SensorId)`, `all()`.
- **`IBindingRepository`**: `bind(PlantId, SensorId, validFrom, std::optional<Quantity> role)`,
  `unbind(PlantId, SensorId, validTo)`, `activeFor(PlantId, QDateTime at)`, `bindings(PlantId)`.
  Enforces the overlap rule via the pure `validateBinding`, throwing `StorageError` on conflict.
- **`IReadingRepository`** is **re-keyed**: `append(SensorId, std::span<const Reading>)`,
  `history(SensorId, Quantity, from, to)`, and the plant-facing
  **`seriesForPlant(PlantId, Quantity, from, to)`** / **`currentForPlant(PlantId)`** that join
  `plant_sensor_bindings` → `readings`, attributing a reading to the plant **only when its `ts_utc`
  falls inside a binding window** (`valid_from <= ts_utc AND (valid_to IS NULL OR ts_utc < valid_to)`),
  then collapsing multi-sensor quantities through `aggregate(..., NewestWins)`.
- Every mutation writes its `change_log` row in the **same transaction** (ADR 0004), `entity` ∈
  {`sensor`, `binding`, `reading`}.
- Each interface gets an `InMemory*` fake validated by the **same behavioural suite** as its
  `Sqlite*` impl (the established parity pattern, `test_repository`/`test_plantrepository`).

## Test matrix

| Area | Test | Asserts |
|------|------|---------|
| pure | `activeBindings` at boundaries | half-open window; instantaneous swap → exactly one active |
| pure | `validateBinding` | redundant no-role allowed; explicit-role overlap → `RoleConflict`; no-role+role allowed |
| pure | `aggregate` NewestWins | freshest wins; role binding preferred; `Average` falls back to newest |
| repo | sensor dedup | `ensure` twice on one handle → same `SensorId`; different handle → new id |
| repo | bind/unbind/activeFor | swap closes one window + opens another; `activeFor(at)` honours time |
| repo | **swap re-homes history** | bind A, append; swap to B, append; `seriesForPlant` returns **both** A's and B's samples, attributed by window |
| repo | multi-sensor (one plant) | two redundant probes both contribute; `currentForPlant` collapses to one value per quantity (newest) |
| repo | **shared sensor (one probe, two plants)** | bind sensor S to plants A and B; append **once** under S; `seriesForPlant(A)` and `seriesForPlant(B)` both return it; unbinding A leaves B's attribution intact |
| migration | v1→v2 | runs on a v1 DB non-destructively; `sensors`/`bindings`/`readings` exist; existing plants/journal intact |

In-memory and SQLite repos run the same suite (`test_bindingrepository`, `test_sensorrepository`,
extend `test_repository`); pure functions in `test_binding` / `test_aggregate`.

## Implementation order (one commit each)

1. **Pure domain**: `Sensor`, `HandleKind`, `PlantSensorBinding`, `BindingError`, `activeBindings`,
   `validateBinding`, `AggregationPolicy`, `aggregate` (NewestWins) + `test_binding`/`test_aggregate`.
2. **Persistence schema v2**: migration + `ISensorRepository`/`IBindingRepository` (+ fakes + SQLite)
   + parity tests + migration test.
3. **Readings re-keyed**: `IReadingRepository` on `SensorId`; `seriesForPlant`/`currentForPlant`
   through bindings; supersede the PlantId-keyed sketch + its test.
4. **`PlantCareModel`** aggregate (`klr` view-model): species + journal + active bindings → `current`
   / `status`; wired through `AppContext`, no `setContextProperty`.
5. **UI**: pair-a-sensor, multi-sensor-in-one-pot, and swap flows; per-screen view-model exposing
   `CurrentReading`/`CareStatus`.

## Deliberately out of scope (this phase)

- **Bucketing + write-cadence gate** and the `ON CONFLICT` replace-vs-keep policy — the history phase (the columns
  exist; the gate does not).
- **`Average` aggregation** and its setting — later phase (enum + fallback only).
- **`CareThresholds` / `care_thresholds` table** (ideal-vs-active ranges) — lands
  with the alert-evaluation UI, not the binding mechanics.
- **Legacy importer** synthesising a binding per device — the history phase.
- **`SensorId` minting from the live device layer** beyond `ensure(handle)` — the registry↔repository
  wiring deepens in later phases; this phase only needs a handle → `SensorId`.

## Files

New under `src/core/`: `binding.h`, `sensor.h`, `aggregate.{h,cpp}`. New under `src/persistence/`:
`isensorrepository.h`, `ibindingrepository.h`, `inmemory{sensor,binding}repository.{h,cpp}`,
`sqlite{sensor,binding}repository.{h,cpp}`; schema v2 in `schema.cpp`; `ireadingrepository.h` +
`inmemoryreadingrepository.{h,cpp}` re-keyed and `sqlitereadingrepository.{h,cpp}` added. New
view-model `src/gui/plantcaremodel.{h,cpp}`. Tests: `test_binding`, `test_aggregate`,
`test_sensorrepository`, `test_bindingrepository`, extended `test_repository`, extended
`test_migration`.
