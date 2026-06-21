# ADR 0017 — Sensor management & data hygiene

**Status:** accepted
**Builds on:** ADR 0005 (plant↔sensor binding), ADR 0006 (history/charts — incl. its time-axis
follow-up), ADR 0011 (always-on sensor ingestion), ADR 0002 (QML-singleton injection).

The binding and history phases made the `sensors` table real (a sensor is registered the moment it is bound, or
legacy-/backup-imported), and ADR 0011 made *any* registered sensor's broadcasts persist the whole
time the app runs. But a sensor was only ever **visible** through a plant: after a swap/detach its
row + reading history lingered with no screen to browse it and no way to prune it (incl. orphans left
when an exclusively-bound duplicate plant is deleted — deferred here from an earlier feature). This phase makes the
registered sensors first-class and lets the user delete a sensor no longer bound to anything.

It also lands the **deferred time-axis tick-quality** fix from ADR 0006 (round hour/day chart ticks),
which the new per-sensor chart and the existing plant chart share.

## Decisions

1. **No schema change.** `readings.sensor_id` and `plant_sensor_bindings.sensor_id` already carry
   `ON DELETE CASCADE`, so deleting a `sensors` row purges its readings + (closed) bindings at the DB
   level. This phase adds only repository methods + a use-case + UI — no migration, no new table.

2. **Repository-boundary deletes (`klr_persistence`).** Three additions, each parity-tested
   SQLite↔in-memory:
   - `ISensorRepository::remove(SensorId)` — the **logged root** of a sensor's removal (one
     `change_log` "delete" row, in one transaction; mirrors `SqlitePlantRepository::remove`).
   - `IBindingRepository::bindingsForSensor(SensorId)` — the sensor-keyed view (open + closed, across
     **all** plants) the registered-sensors model needs to judge bound/unbound without iterating
     every plant. (The existing binding queries are per-plant only.)
   - `IReadingRepository::removeForSensor` / `IBindingRepository::removeForSensor` — **silent** cleanup
     (no `change_log`, matching the cascade precedent: a parent delete's cascade is not logged
     per-child, and readings carry no change-log at all). They exist so the in-memory fakes — whose
     repos are independent objects — converge with the SQLite cascade under one use-case code path.

3. **`SensorDeleter` use-case (`klr_persistence`, pure orchestration).** `remove(SensorId) ->
   std::expected<void, SensorDeleteError>`. It **refuses (`StillBound`) while *any* binding — open OR
   closed — references the sensor**, not merely an open one: a closed binding still ties the sensor's
   readings to that plant's history (history follows the plant through the window, ADR 0005), and
   **detaching only closes the binding, it does not remove it**. A binding is removed only when its
   plant is deleted (FK cascade), so "no bindings" == "no plant uses it" == a true orphan — the only
   safe time to delete. Returns `NotFound` for an unknown id. Otherwise clears readings → bindings →
   the sensor row. Like `PlantDuplicator` it is **not** one cross-repo transaction, but a delete is
   monotonic (nothing to compensate). Parity-tested against both repo flavours (`test_sensordeleter`,
   incl. that detaching does **not** make a sensor deletable).

4. **`RegisteredSensorsModel` (`klr_gui`).** A thin `QAbstractListModel` over the sensor table +
   `bindingsForSensor` + the reading store + scanner liveness, mirroring `SensorStatusModel`: per row
   model / address / a **bound** flag (**any** binding, open or closed — i.e. "referenced by a plant",
   which is exactly the delete guard) / liveness / battery (authoritative, from the reading store) /
   last-seen / gatt-open. Refreshed on the same 1 s connectivity heartbeat and alongside
   `SensorStatusModel` on ingest / history-sync / attach / detach / import / restore.

5. **AppContext surface.** A `registeredSensors` model property; a dedicated **per-sensor**
   `SeriesModel sensorHistory` (owned by value, like `PlantCareModel::history` — plant-agnostic, **no
   care band**, since thresholds live on a plant and a sensor may serve none or many);
   `selectedSensorId` + `selectedSensorBound`; and three invokables —
   `selectRegisteredSensor` (drives the shared `selected*` status by the sensor's **handle**, so the
   existing `refreshSelectedStatus` machinery works for an offline / not-currently-scanned sensor),
   `loadSensorHistory` (display-unit converted), and `removeRegisteredSensor` (delegates to
   `SensorDeleter`, refuses while bound, refreshes the registered list + plant views, reports via the
   `sensorRemoved(ok, message)` signal). `sensorHistoryQuantities(sensorId)` returns the quantities
   that actually have data as `{value,label}` (labels from C++) for the detail screen's selector.

6. **UI.** `Route::SensorDetail` (the documented enum extension point). `ScanScreen` grows a
   **"Registered sensors"** section below the live BLE scan, bound to `AppContext.registeredSensors`,
   with a liveness dot and a **`potted_plant` icon + hover tooltip** marking a sensor bound to a plant
   (replacing an earlier "bound/unbound" word). The live-scan list above is filtered to **exclude
   already-registered devices** (`DeviceSortFilterModel`'s `excludeRegistered` consulting the sensor
   repo, re-run on the registered model's reset) so a registered sensor never appears in both lists;
   the pairing picker keeps showing them (you may re-attach a known-but-unbound sensor). A row selects
   the sensor and pushes `SensorDetail`. The new **`SensorDetailScreen`** shows identity + live status,
   a quantity selector driving the per-sensor history chart (the same `SeriesModel`/QtGraphs path as
   the plant chart, sans band), and a **"delete sensor data"** action — disabled while the sensor is
   bound, with copy that explains the data belongs to a plant's history (no misleading "detach first")
   — behind a fixed-size confirm dialog (the `PlantSettingsScreen` pattern, to dodge the
   Material-`Dialog` implicitHeight loop); it pops back to the list once `sensorRemoved(true)` arrives.
   **The plant-detail "Sensors" tab opens the same `SensorDetailScreen`** (via `selectRegisteredSensor`
   on the row's sensor id), so a sensor reads identically whether reached from a plant or the Sensors
   screen (it formerly pushed the live-broadcast `LiveScreen`).

7. **Time-axis tick quality (ADR 0006 follow-up).** `niceTimeAxis()` now picks a round step from a
   ladder (1h/3h/6h/12h/1d/1w/~1mo) sized to the span, **floors `min` / ceils `max`** to a step
   multiple, returns the resulting division count, and **coarsens** (doubles the step) if the count
   would breach QtGraphs' `[0,100]` clamp; a degenerate span becomes a 1 h window. The struct gained
   snapped `minMs`/`maxMs`; `SeriesModel` adopts them as its reported `tMin`/`tMax`/`tMinDate`/
   `tMaxDate` (points keep raw timestamps, inside the outward-rounded window) so `DateTimeAxis.min`/
   `max` align with the ticks — gridlines land on the hour/day boundary. Steps are epoch multiples,
   so hour/day ticks are exact in UTC; week/month rungs are approximate (only coarse labels).

## Consequences

- The Sensors screen lists every registered sensor with its bound/unbound status; selecting one shows
  its full per-sensor history and offers a guarded delete.
- A sensor's data can be deleted **only when no plant references it** (no open *or* closed binding):
  readings key on the sensor and plants derive through bindings, so deleting while any plant still
  uses it would tear a hole in that plant's history. Detaching doesn't unlock deletion (it only closes
  the binding) — proven by `test_sensordeleter`. A sensor becomes deletable when its last plant is
  deleted (the FK cascade removes the binding, leaving an orphan).
- Chart ticks read as round clock/calendar times across both the plant and per-sensor charts.
- One sync nuance: a sensor delete logs a single `sensor`/`delete` row; the cascaded reading/binding
  removals are unlogged (consistent with how cascades and bulk telemetry already behave — a reducer is
  a later phase).

## Implementation order (one commit each, `ctest` green at each)

1. `ISensorRepository::remove` + `IBindingRepository::bindingsForSensor`/`removeForSensor` +
   `IReadingRepository::removeForSensor` (+ parity tests; `test_changelog` for the sensor delete row).
2. `SensorDeleter` use-case (+ `test_sensordeleter`).
3. `niceTimeAxis` snapping in `core/axis` (+ `test_axis`) and `SeriesModel` surfacing the snapped
   bounds (+ `test_seriesmodel`).
4. `RegisteredSensorsModel` + AppContext wiring (registeredSensors / sensorHistory /
   selectRegisteredSensor / loadSensorHistory / removeRegisteredSensor; + `test_appcontext`).
5. `Route::SensorDetail`, the ScanScreen section + `SensorDetailScreen` (+ `test_navigation`).
6. This ADR.

## Tests

`test_sensorrepository` (remove parity), `test_bindingrepository` (`bindingsForSensor` +
`removeForSensor` across a shared sensor), `test_changelog` (sensor delete logs one row),
`test_sensordeleter` (refuse-while-bound, unbound delete cascades, shared-sensor guard, NotFound),
`test_axis` + `test_seriesmodel` (snapped ticks + bounds), `test_appcontext` (`loadSensorHistory`
fills the series; `removeRegisteredSensor` deletes unbound / refuses bound), `test_navigation`
(`SensorDetail` is a section detail that pops to Sensors). The QtGraphs chart + the D-Bus-free QML
remain the only untested pieces by design (covered by qmllint + cachegen).

## Out of scope / follow-ups

- Recording advertisement values for known-but-unbound sensors (overlaps the postponed
  advertisement-first monitoring work) — kept out unless wanted.
- True cross-repository atomicity for the delete (a SAVEPOINT unit-of-work) — the monotonic delete
  doesn't need it; revisit alongside `PlantDuplicator`'s same deferral.
- A reducer that applies `change_log` deletes on a sync peer (when sync lands).
- True calendar-month snapping for the time axis (the ~30 d rung is an approximation).
