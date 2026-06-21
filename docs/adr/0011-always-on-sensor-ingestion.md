# ADR 0011 — Always-on, sensor-level reading ingestion

**Status:** built
**Builds on:** ADR 0005 (the N:N plant↔sensor binding + the sensor-keyed `readings` table) and
ADR 0006 (the hourly `bucketStartMs` key + the `WriteCadenceGate`). This ADR moves *where* the gate
and the ingest trigger live; it changes **no** storage shape and needs **no** migration.

## Problem

A sensor's stored data only advances while a *specific screen is open*, so a plant's readings go
stale the moment the user navigates away — exactly the opposite of what a monitoring app should do.
Two screen-scoped gates cause this:

1. **Scanning is screen-owned.** `BleScanner` is the passive advertisement listener (it never opens
   a GATT connection — coin-cell-friendly, per ADR 0001 and the BLE design). But the only
   callers of `AppContext::startScan()`/`stopScan()` are `ScanScreen.qml:18-19`
   (`Component.onCompleted` / `onDestruction`) and the `PlantSettingsScreen.qml:260-261` pairing
   dialog (`onOpened` / `onClosed`). Leave those and the listener stops — **zero** advertisements
   arrive. The agent itself is built to run forever (`setLowEnergyDiscoveryTimeout(0)`); nothing
   keeps it running.
2. **Ingest is plant-scoped.** Even while scanning, the `BleScanner::deviceChanged` handler
   (`appcontext.cpp:129-145`) only stores a reading `if (m_care && m_care->hasPlant())` — i.e. when
   a plant **detail** screen is open — and then only for that one plant. `PlantCareModel::ingest`
   (`plantcaremodel.cpp:246-274`) walks *the open plant's* active bindings, matches a handle, gates,
   and appends. A broadcast from a sensor bound to a **different** (or no currently-open) plant is
   dropped on the floor. The `WriteCadenceGate` is a member of `PlantCareModel`
   (`plantcaremodel.h:111`), so its dedup state is per-open-plant and resets on navigation.

The persistence layer is already correct and does **not** change: `readings` is keyed by
`sensor_id` alone, and `IReadingRepository::currentForPlant`/`seriesForPlant` already attribute a
sensor's samples to *every* plant bound during the sample's time window (`readingattribution.h`).
The bug is purely that ingestion is triggered and gated at the wrong layer.

## Decisions

### 1. Ingestion is a sensor-level concern, not a plant-level one

Persist for **any registered sensor** (one the user has paired — resolvable via
`ISensorRepository::findByHandle`), the entire time the app runs, regardless of which screen is
open and regardless of how many plants the sensor feeds. Broadcasts from unpaired BLE devices in
range are dropped (they have no `Sensor` row). Bound plants are then *notified* — they never own the
write.

### 2. A `ReadingIngester` (new, pure-ish, unit-testable — `klr_gui`)

Extract the gate + the store decision into a small non-QML class that depends only on
`ISensorRepository`, `IReadingRepository`, and the injected `Clock`, and **owns the
`WriteCadenceGate`** (moved out of `PlantCareModel`):

```cpp
// Plant-agnostic. Resolve handle → registered Sensor; gate per (SensorId, Quantity);
// append admitted readings. Returns the SensorId actually written, or nullopt when the
// handle is unregistered or every reading was a redundant in-bucket repeat.
std::optional<SensorId> ingest(HandleKind kind, const QString &handleValue,
                               std::span<const Reading>);
```

The gate is now process-global (one ingester for the app's life), so dedup is correct across plant
navigation and across the N plants a sensor feeds — not reset per screen. Correctness of *history*
still does not depend on the gate (the repo buckets every row it is handed, ADR 0006); the gate
remains only the write-rate optimisation that the process-global dedup fix made safe.

`PlantCareModel` loses `ingest()` and `m_gate` and becomes a pure **reader**
(`currentForPlant`/`seriesForPlant`), which already fans out across all of its plant's bindings.

### 3. Scanning starts at the composition root, not a screen

`main.cpp` starts the scanner at startup (and stops it on exit), decoupled from any screen.
`ScanScreen` keeps its manual refresh button but drops `Component.onDestruction: stopScan()`; it
becomes a *view* of an already-running scan. The pairing dialog may still `startScan()` (idempotent)
but must not `stopScan()` out from under the background listener. (Mobile background-scan policy is
the mobile build's problem and explicitly out of scope here — this ADR is the Linux-desktop "always-on while the
app is running" fix.)

### 4. Notify the bound plants after an admit

`AppContext` wires `scanner.deviceChanged` → `m_ingester.ingest(...)`. On a non-empty result it
refreshes the live UI: the `PlantListModel` rows for the plants bound to that sensor (looked up via
the binding repo) and the open `PlantCareModel` if its plant is among them, then emits
`careChanged()`. The selected-device/RSSI bookkeeping in the existing handler is unchanged. A
refresh-all of `PlantListModel` is an acceptable first cut given the model size; the surgical
"rows for plants bound to this sensor" path is preferred and cheap once the binding repo exposes a
`plantsForSensor`-style lookup.

## Test matrix

| Area | Test | Asserts |
|------|------|---------|
| ingester | unregistered handle | `findByHandle` miss → nothing stored, returns `nullopt` |
| ingester | registered sensor, no plant open | reading is stored (the core regression — no screen needed) |
| ingester | gate parity | same value in one bucket → one write; new bucket **or** changed value → re-admit (mirrors `test_bucket`) |
| ingester | shared sensor | a sensor bound to two plants → one stored row, **both** plants see it via `currentForPlant` |
| ingester | gate survives navigation | switching the open plant does not reset dedup for an unrelated sensor's series |
| gui | notification | an admit for sensor S refreshes exactly the `PlantListModel` rows whose plant is bound to S |

## Implementation order (one commit each)

1. **gui**: `readingingester.{h,cpp}` (owns `WriteCadenceGate`, depends on sensor+reading repos +
   `Clock`) + `test_readingingester`. No call sites yet.
2. **gui**: rewire `AppContext` — `deviceChanged` → `m_ingester.ingest`; refresh affected
   `PlantListModel` rows + open `PlantCareModel`; emit `careChanged()`. Delete
   `PlantCareModel::ingest` + `m_gate`; trim `plantcaremodel` tests accordingly.
3. **app**: start the scanner in `main.cpp` at startup; stop on exit.
4. **gui (qml)**: drop `ScanScreen`'s `onDestruction: stopScan()`; keep the refresh button; ensure
   the pairing dialog no longer calls `stopScan()`.

## Deliberately out of scope (this ADR)

- **Mobile background scanning / OS background-execution policy** — the mobile build.
- **Unbound-sensor data pruning** and the registered-sensor browser — a later feature (this ADR makes "data keeps
  arriving for paired-but-not-open sensors" true, which is precisely what those hygiene tools manage).
- **Notification *evaluation*** on the freshly-ingested readings — a later feature (this ADR is its prerequisite:
  threshold-transition and watering-due alerts are only meaningful once data advances off-screen).
- The **`AdvSnapshotAccumulator`** multi-frame coalescing — still deferred (ADR 0006 Track B).

## Files

New under `src/gui/`: `readingingester.{h,cpp}`. Changed: `appcontext.{h,cpp}` (own the ingester,
rewire `deviceChanged`), `plantcaremodel.{h,cpp}` (drop `ingest`/`m_gate`, reader-only),
`app/main.cpp` (start scan at root), `gui/qml/ScanScreen.qml` + `gui/qml/PlantSettingsScreen.qml`
(scan-lifecycle). Tests: new `test_readingingester`; trimmed `test_plantcaremodel`.
