# ADR 0014 — GATT connected-mode history backfill + battery-on-connect

**Status:** built
**Builds on:** ADR 0005 (sensor-keyed `readings`, N:N binding), ADR 0006 (hourly `ts_bucket` +
idempotent upsert append), ADR 0011 (always-on advertisement ingestion — this is its connected-mode
counterpart). **New schema:** v4 (`sensor_sync_state`); a transactional, non-destructive migration.

## Problem

The app listens to advertisements the whole time it runs (ADR 0011), but advertisements only carry
**now** — they cannot recover the hours the app was closed. Every BLE plant sensor keeps an on-device
**history log** (Flower Care / RoPot store ~16 days at one entry/hour), reachable only over a GATT
connection. Today klorophylle never reads it: a plant's chart simply has a gap for any time the app
was off. Two further facts make a connected path mandatory rather than optional:

1. **Battery is connection-only** for Flower Care / RoPot — it is *never* in their MiBeacon
   advertisement. The only way to keep a fresh battery level is to read it while
   connected for something else.
2. **A connection costs the sensor's coin-cell**; advertisements are free. So a history sync must be
   **infrequent** (cadence-gated) and **bounded** (read only the entries newer than the last sync),
   or it trades the whole battery-respecting premise away.

klorophylle's existing GATT layer (`GattSession` + declarative `GattReadProfile`) is strictly
**one-shot** (connect → read N characteristics → decode → disconnect). History is **stateful and
iterative** (enter history mode → read device clock → read entry count → loop {select entry, read
entry}), and on current Flower Care firmware the history service is gated behind an **RC4 MiBeacon
handshake** (challenge/response) — which klorophylle does not have (the GPL RC4 *dependency* was
dropped). So this needs a new session type, a ported handshake, per-sensor sync-state, and a
cadence policy.

## Decisions

### 1. History entries are ordinary `Reading`s with old timestamps — reuse the write path

`IReadingRepository::append` already floors any timestamp to `ts_bucket` and upserts per
`(sensor, quantity, bucket)` with "NULL never erases" semantics (ADR 0006). A downloaded entry is
just a `Reading` whose `timestamp` is in the past and whose `provenance` is `History`. Re-reading a
few entries we already have is therefore **idempotent** — which is why v1 needs **no DB-gap
detection**: we read the newest *N* entries since the last sync (plus a small margin) and let the
upsert absorb the overlap.

### 2. Per-sensor sync state in a new device-local table (schema v4)

A new `sensor_sync_state(sensor_id PK → sensors(id) ON DELETE CASCADE, last_history_sync TEXT)` behind
a new `ISyncStateRepository` (in-memory + SQLite, parity-tested like every repo, ADR 0004). It is
**device-local operational state, NOT change-logged** — each replica tracks its own GATT connects;
syncing the marker across devices would be wrong. `Sensor` stays an immutable value type (the marker
does not belong on it). **The marker is set to `now` on a *completed* sync — never derived from the
last entry's timestamp** (the failure mode to avoid: a trimmed read then leaves it in the past and
re-triggers forever — `advertisement-monitoring.md`).

### 3. A declarative `GattHistoryProfile` + a pure decoder seam, an imperative `GattHistorySession`

Keep the wire knowledge pure and unit-tested (the seam the advertisement + one-shot GATT paths
already use). `GattHistoryProfile` (Qt-Bluetooth-free, `klr_devices`) declares the history service +
characteristic UUIDs, the `modeCommand`/`addressFor(index)` byte builders, `entriesPerHour`, the
`needsHandshake`/`productId` + handshake char UUIDs, and the **pure decoders**
(`decodeHistoryCount`, `decodeDeviceTimeSecs`, `decodeHistoryEntry(bytes, deviceWallEpochMs)`,
`decodeBattery`). `DeviceFlowerCare` + `DeviceRoPot` (same family) return one from a new
`Device::gattHistoryProfile()` (default `nullopt`, mirroring `gattProfile()`).

`GattHistorySession` (`klr_ble`, sibling to `GattSession`) is the imperative orchestrator —
challenge/response handshake cannot be declarative, and the read loop is stateful. It is
hardware-verified (no BLE in CI), exactly like `GattSession`. The **window size** it fetches is the
pure `historyEntriesToRead(...)` (`klr_core`), unit-tested.

### 4. RC4 MiBeacon handshake, ported and pure

A small `mibeacon_auth.{h,cpp}` (`klr_devices`, GPL header): pure `rc4(key, data)` + `mibeaconToken(mac,
productId)` (WatchFlower's `mixA` derivation). The *crypto* is pure and unit-tested with WatchFlower's
known vectors; the *handshake sequence* (write token → await → write finish) is driven imperatively by
`GattHistorySession` using it. Full Flower Care/RoPot firmware coverage is worth porting the
~1-file helper.

### 5. Battery read on every connect

`GattHistorySession` reads the battery characteristic (`0x1a02` for Flower Care: byte0 = %, bytes 2..
= firmware ASCII) at the end of every session and appends a `Quantity::Battery` reading — the only
moment we are connected, so it is effectively free. (Firmware-string persistence is deferred —
`Sensor` has no field for it yet.)

### 6. Cadence policy in a `HistorySyncController`, launch + periodic

A `HistorySyncController` (`klr_gui`) owns *when*: on a startup grace delay and a periodic timer it
builds the due-list via the pure `dueForSync(sensors, syncState, now, cadence)` (registered sensors
whose model has a history profile and whose last sync is null or older than the cadence), and drives
**one** `BleScanner::syncHistory` at a time (single BLE radio). On completion it appends the readings
(`Provenance::History`) + battery, stamps `last_history_sync = now`, and refreshes the affected
plant views. Cadence is `historySyncIntervalHours` (default **6**) with a `historySyncEnabled` toggle,
both in `SettingsStore` (device-local prefs, out of the sync schema). A manual "sync now" path too.

## Test matrix

| Area | Test | Asserts |
|------|------|---------|
| window math | never synced | `lastSync` absent → read the full `entryCount` |
| window math | incremental | `lastSync` 3 h ago, 1/h → ~3 (+margin) entries, clamped to `entryCount` |
| window math | last-sync = now guard | controller stamps `now` on completion, not the last entry's ts |
| due-list | cadence gate | a sensor synced < cadence ago is **not** due; null/older **is** |
| decoders | entry | the doc's 16-byte vector → exact temp/lux/moisture/conductivity + absolute ts from `deviceWallEpochMs` |
| decoders | count / device-time / battery | doc vectors → uint16 count, uint32 uptime secs, battery % |
| auth | RC4 + token | WatchFlower's known vectors for PID 0x0098 (Flower Care) and 0x015D (RoPot) |
| sync-state | repo parity | in-memory == SQLite for set/get/absent + v3→v4 migration round-trip |
| persistence | idempotent backfill | appending overlapping history entries twice → one row per bucket, values intact |

## Implementation order (one commit each, `ctest`-green at each)

**Slice A — pure foundations (CI-tested, no live BLE):**
1. `core/historysync.{h,cpp}` + `test_historysync` (window + due-list).
2. Flower Care pure history decoders in `device_flowercare.h` + `test_historydecoders`.
3. `devices/mibeacon_auth.{h,cpp}` + `test_mibeaconauth`.
4. Schema v4 + `ISyncStateRepository`/`InMemory`/`Sqlite` + `test_syncstaterepository`.
5. `devices/gatthistoryprofile.h` + `Device::gattHistoryProfile()` + Flower Care/RoPot profiles.

**Slice B — live session (hardware-verified):**
6. `ble/gatthistorysession.{h,cpp}` (handshake + read loop + battery).
7. `BleScanner::syncHistory(id)` + signals.

**Slice C — policy / cadence / root / UI:**
8. `gui/historysynccontroller.{h,cpp}`.
9. `SettingsStore` cadence + toggle + Settings UI row.
10. `app/main.cpp` wiring (construct + grace-start).
11. `AppContext::syncHistoryNow()` + per-sensor "last synced / syncing" status in the Sensors UI.

## Deliberately out of scope

- DB-gap "missing-hours" optimization — append idempotency makes it non-essential for v1.
- Parrot Flower Power / Pot and LYWSD02 history (different protocols).
- Firmware-version persistence on `Sensor`.
- Mobile background sync / OS background-execution policy — the mobile build.
- Advertisement-grace-before-connect for *realtime* reads — separate from history.

## Files

New under `src/core/`: `historysync.{h,cpp}`. Under `src/devices/`: `mibeacon_auth.{h,cpp}`,
`gatthistoryprofile.h`. Under `src/persistence/`: `isyncstaterepository.h`,
`inmemorysyncstaterepository.h`, `sqlitesyncstaterepository.{h,cpp}`. Under `src/ble/`:
`gatthistorysession.{h,cpp}`. Under `src/gui/`: `historysynccontroller.{h,cpp}`.
Changed: `devices/device.h`, `devices/device_flowercare.h`, `devices/device_ropot.h`,
`persistence/schema.{h,cpp}`, `ble/blescanner.{h,cpp}`, `gui/appcontext.{h,cpp}`,
`gui/settingsstore.*`, `app/main.cpp`, the Settings + Sensors QML, the `CMakeLists.txt` files.
Tests: `test_historysync`, `test_historydecoders`, `test_mibeaconauth`, `test_syncstaterepository`.
