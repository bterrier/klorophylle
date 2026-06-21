# ADR 0006 — Persistence, history & charts

**Status:** accepted · **Builds on:** ADR 0005 (the sensor-keyed
`readings` table + the plant↔sensor binding window landed there; the `ts_bucket`/`source`/
`observed_by` columns were created empty so this phase fills them with **no** migration).

This phase turns live readings into **stored history** and **charts**, and brings forward existing users'
data. It is where two persistence issues are addressed *by design*: a write-gate misaligned with the
bucket key skips a bucket ~once every few days; and `ChartHelper` mutates QML-created series. The end state is a complete single-device Linux desktop
product (plant-first, journal, multi-sensor, swap, **history, charts, legacy import**) — offline, no
sync, no AI.

This ADR fixes the bucketing rule, the write-cadence gate, the append-vs-replace policy, the chart
view-model ownership, and the legacy-import mapping, so the work lands as small independently-tested
commits.

## Decisions

### Bucketing & the write-cadence gate (pure, clock-injected — `klr_core`)

WatchFlower used two *different* notions of time: the history PK rounded the timestamp to a
bucket, while a separate rolling-60-minute gate (`needsUpdateDb_mini()`) decided *when* to write —
and because the gate's window drifted relative to the absolute clock-hour boundary, a bucket
occasionally received no write at all. The fix is **one clock for both**.

- `bucketStartMs(qint64 tsMs, qint64 bucketMs) -> qint64` — pure floor of a timestamp to its
  bucket boundary. `kBucketMs` defaults to **hourly** (`3'600'000`); half-hourly is a parameter, not
  a fork. A reading's `ts_bucket` is `bucketStartMs(reading.ts)` — derived from the **reading's own
  timestamp**, which in the live path is stamped from the injected `Clock`. So the dedup key and the
  sample time can never disagree.
- `WriteCadenceGate` — keyed per `(SensorId, Quantity)`,
  `admit(key, nowMs, value) -> bool` returns true the **first** time a series enters a new bucket
  **or** its value changes, and false for an identical re-broadcast within the same bucket. Because
  `admit` buckets the *same* `nowMs` it is gated on, **every bucket that receives a sample is
  admitted at least once — a bucket can never be skipped** (the bucket-skip regression, asserted by a
  multi-day test). Admitting on a value change too keeps the live "current" display fresh (the repo
  upserts the change into the same bucket row) instead of pinning it to the hour's first sample.
  The gate is a pure value object (no clock member); the caller passes the reading's timestamp,
  which is clock-derived in the live path.

The gate lives in `klr_core` and is wired into the **live ingest path** (`PlantCareModel::ingest`)
so production does not issue a DB write per advertisement (~1/s). Correctness does **not** depend on
the gate — the repository still buckets every row it is handed — the gate is the write-rate
optimisation that the bucket-skip fix makes safe.

### Append-vs-replace (decided deliberately)

PK stays `(sensor_id, quantity, ts_bucket)` — **one row per bucket**. On conflict the **latest
sample in the bucket wins**, *except* a NULL (absent) value never overwrites a stored present one —
absence is not news:

```sql
INSERT INTO readings(...) VALUES(...)
ON CONFLICT(sensor_id, quantity, ts_bucket) DO UPDATE SET
  value = excluded.value, ts_utc = excluded.ts_utc,
  source = excluded.source, observed_by = excluded.observed_by
WHERE excluded.value IS NOT NULL;   -- a later absent reading does not erase a good one
```

The stored `ts_utc` is the sample's real timestamp (not the bucket start); `ts_bucket` is only the
dedup key. The `InMemory*` fake applies the identical rule so the parity suite stays meaningful.

### Provenance (`source` / `observed_by` populated now — substrate for the probe & sync)

Every row carries `source` (the per-`Reading` `Provenance`) and `observed_by` (the node that saw
it). Live/advertisement writes stamp `observed_by = Database::replicaId()`; imported rows use
`source = Provenance::History` and an empty `observed_by`. These are required by the probe (goal #4)
and sync (goal #5) and are cheap to land with the history that needs them — deferring them would
force a backfill migration later.

### Charts — invert the inverted ownership (`klr_core` + `klr_gui`)

Adopt **QtGraphs 2D** (`GraphsView` + `LineSeries` + `ValueAxis`), the forward path per the 6.11
docs. QtGraphs does **not** auto-range its axes, so the view-model must publish the range:

- A pure `niceAxis(lo, hi, targetTicks) -> {min, max, tickInterval}` in `klr_core` rounds a data
  range out to "nice" 1/2/5×10ⁿ bounds and a tick step. Unit-tested with literal data (it replaces
  WatchFlower's `applyTimeAxis`).
- `SeriesModel : QAbstractListModel` (`klr_gui`, `QML_ELEMENT`) exposes the points as `x`/`y` roles
  and publishes `axisMin`/`axisMax`/`tickInterval` (value) + `tMin`/`tMax` (time) as `Q_PROPERTY`s.
  It is **filled in C++** from `IReadingRepository::seriesForPlant`; QML's `LineSeries` binds to it
  through the model and **C++ never reaches into a QML-created series object** (the inverted-ownership fix).

QtCharts remains the documented escape hatch only if a feature gap blocks a screen — not a default.

### Legacy import (`LegacyImporter` — `klr_persistence`)

`LegacyImporter` opens an existing WatchFlower `data.db` **read-only** and maps it into the new
schema through the *repositories* (so the change-log + FK discipline hold):

- `devices` → a `Sensor` (handle = the stored MAC; `model` from `deviceModel`) **and**, where the
  row names a plant, a `Plant`; plus a synthesised `PlantSensorBinding` with
  `valid_from = plantStart` (or `firstSeen`), open-ended.
- `plants` / `plantJournal` → `Plant` / journal entries.
- `plantData` / `thermoData` / `sensorData` → `readings`: the **wide** rows (one column per
  quantity) are **un-pivoted** to `(quantity, value)` pairs and `-99` becomes `NULL`.
  `ts_bucket = bucketStartMs(ts)`, `source = History`.

A CI test builds a small **old-schema** fixture DB in a temp file, imports it, and asserts the
mapping; a separate v0→vN round-trip asserts the migration runner is **non-destructive** (no silent
`DROP`). The importer honestly cannot recover environmental history from DBs already upgraded
through the old destructive v2→v3 `sensorData` drop — a test documents that limitation
(a known persistence open question).

### Track B — devices already on the pure path

WatchFlower decoded advertisements inline in ~10 device classes. In klorophylle this is
**already satisfied**: every `device_*.h` decodes through `AdvertisementParser::decodeXxx`
(`test_advertisement`/`test_devices` cover them). This phase adds no rework here. The Flower Care multi-frame
`AdvSnapshotAccumulator` (coalescing the per-object frame stream into one record) is the *only*
remaining decoder nicety and is deferred — the current per-quantity latest-wins merge in
`BleScanner` is sufficient for the bucketed history this phase delivers.

## Test matrix

| Area | Test | Asserts |
|------|------|---------|
| pure | `bucketStartMs` | floors to the boundary; sub-bucket times share a bucket; exact-boundary is its own |
| pure | `WriteCadenceGate` multi-day | over an N-day stream, one admit per occupied bucket — **no skip, no dup** (the multi-day skip/dup regression) |
| pure | `niceAxis` | literal ranges → expected min/max/tick; degenerate (flat) range handled |
| repo | bucketed upsert | two samples in one hour → one row (latest wins); NULL does not erase a present value; parity in-mem == SQLite |
| repo | provenance | live write stamps `observed_by = replicaId`; `source` round-trips |
| import | mapping | fixture old DB → sensors/plants/bindings/readings; wide rows un-pivoted; `-99` → NULL |
| import | round-trip | v0→vN migration runs non-destructively; pre-existing rows intact |
| vm | `SeriesModel` | points + axis props from a literal series; empty series → safe defaults |

## Implementation order (one commit each)

1. **core**: `bucket.{h,cpp}` (`bucketStartMs`, `WriteCadenceGate`) + `niceAxis` in a small
   `axis.{h,cpp}` + `test_bucket`, `test_axis`.
2. **persistence**: bucketed write path in both reading repos (ts_bucket, append-vs-replace,
   provenance/observed_by) + extended `test_repository`; wire the gate into `PlantCareModel::ingest`.
3. **persistence**: `LegacyImporter` + `test_legacyimport` (fixture old DB) + round-trip in
   `test_migration`.
4. **gui**: `SeriesModel` view-model + `AppContext` history wiring + `test_seriesmodel`.
5. **gui**: QtGraphs history chart QML screen reached from the plant detail screen; link `Qt6::Graphs`.

## Follow-up — time-axis tick quality (DONE, see ADR 0017)

**Resolved** (`docs/adr/0017`): `niceTimeAxis()` now snaps the bounds and reports a real
division count, so ticks land on the hour/day boundary. The original limitation is kept below for
context.

Before that fix, the time axis was **not** as "nice" as the value axis — a genuine QtGraphs
limitation worth recording for whoever next touches the chart.

`niceAxis()` snaps the Y `ValueAxis` out to 1/2/5×10ⁿ bounds and feeds `ValueAxis.tickInterval` a
real **spacing** that QtGraphs honours. The X `DateTimeAxis` *cannot* be treated the same way:
`DateTimeAxis.tickInterval` is **not** a millisecond spacing — QtGraphs clamps it to `[0,100]` and
uses it as the **number of divisions** (verified in `axisrenderer.cpp updateAxis()`; the trap and the
clamp are documented in `core/axis.h`). Feeding it a ms span (the obvious first attempt) clamps to
100 and floods the axis with gridlines. So `niceTimeAxis()` returns only a small division count
(`targetTicks-1`) and passes the **raw** `tMin`/`tMax` through unsnapped — which means the axis
divides the span into N equal parts and tick labels fall on arbitrary clock times ("13:47") rather
than round ones ("13:00"). This is the limitation behind the "QtGraphs can't draw a tick every hour"
complaint: you genuinely cannot ask for an hourly *interval*, only N evenly-spaced ticks.

**The fix** (shipped later — pure, unit-tested, no library change): give the time axis the same
nice-snapping the value axis has. Choose a calendar step from a ladder (1h/3h/6h/12h/1d/1w/1mo) sized to the span,
**floor `tMin` / ceil `tMax` to that step boundary**, and set the division count to
`(snappedMax−snappedMin)/step` (drop to a coarser step when that would exceed the [0,100] clamp).
Ticks then land on the hour/day boundary. `niceTimeAxis()` grows to return the snapped
`tMin`/`tMax` alongside the count + format; `SeriesModel` exposes the snapped bounds to
`HistoryChartScreen.qml`'s `DateTimeAxis.min`/`max`. Covered by extending `test_axis` in the existing
literal-range style (e.g. a multi-hour span → hourly ticks on the hour; a multi-week span → daily
ticks; a span that would exceed 100 divisions → coarser step). Note the cosmetic trade-off: snapping
outward means the line no longer touches the left/right plot edges — acceptable, and matches what the
Y axis already does.

## Deliberately out of scope (this phase)

- **`Average` aggregation** + its setting (still `NewestWins`; ADR 0005).
- **`AdvSnapshotAccumulator`** multi-frame coalescing (deferred, see Track B above).
- **Per-thread `ConnectionPool`** — arrives with the headless probe that shares the file.
- **Reading sync / change-log for `readings`** — bulk telemetry stays out of the log until sync lands.
- **`CareThresholds` table** and alert evaluation — lands with the care-status UI.

## Files

New under `src/core/`: `bucket.{h,cpp}`, `axis.{h,cpp}`. New under `src/persistence/`:
`legacyimporter.{h,cpp}`. New under `src/gui/`: `seriesmodel.{h,cpp}`, `qml/HistoryChartScreen.qml`.
Changed: `sqlitereadingrepository.cpp` / `inmemoryreadingrepository.cpp` (bucketing),
`plantcaremodel.{h,cpp}` (gate + history series), `appcontext.{h,cpp}` (chart wiring), `schema.h`
docstring. Tests: `test_bucket`, `test_axis`, `test_seriesmodel`, `test_legacyimport`, extended
`test_repository` + `test_migration`.
