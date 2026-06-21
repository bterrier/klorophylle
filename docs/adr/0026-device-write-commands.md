# ADR 0026 — Device write commands ported from WatchFlower

**Status:** accepted · **Builds on:** ADR 0014 (the GATT
sessions + the MiBeacon RC4 handshake), ADR 0002 (composition root / injected clock). **No schema
change.**

## Problem

klorophylle ported every *decoder* from WatchFlower (advertisement, realtime GATT, history) but
**no** write action; its `GattSession`/`GattHistorySession` only read. The remaining capability is
the GATT **write/command** byte-sequences behind the device *actions* — make the LED blink, water
the plant, calibrate, clear the on-device log, set the device clock. Those byte-sequences are
field-verified knowledge from WatchFlower (GPL) that exists nowhere else, so this ADR ports them
into klorophylle.

**Scope:** *capture + an execution seam, no UI.* This ADR records the ported
bytes, adds pure unit-tested byte builders + a descriptor surface on the device layer, and adds a
`GattCommandSession` that can actually execute a command from C++. **No** QML buttons /
capability-gated action affordances — device control is **not** committed as a launch UI feature;
wiring it into a screen stays a later, optional phase.

## Decisions

### 1. Commands live on the `Device` subclasses (`gattCommands(now)`), not on the registry.

An obvious home would be a `commands` block on the relevant `DeviceRegistry` entries. But
klorophylle's `DeviceRegistry` is deliberately minimal — `{matcher, factory}` only; **all** device
metadata (`model()`, `type()`, `gattProfile()`, `gattHistoryProfile()`) already lives on the
`Device` subclasses (the established design, fixing the two-drifting-switches pitfall). The
intent-faithful home for command data ("data, not scattered code; one place lists a device's
capabilities") is therefore a new optional virtual mirroring the existing read/history profiles:

```cpp
virtual QList<GattCommand> gattCommands(const QDateTime &now) const { return {}; }
```

The **presence** of a command of a given `DeviceAction` *is* the capability — no separate flag
(exactly as `gattProfile()` presence == "has a connection path"). This is a conscious divergence
from that registry-entry placement, kept faithful to its rationale.

### 2. Byte payloads come from pure static builders; the clock is injected.

Each action's bytes are produced by a pure static function on the device class — the same
pure-decoder seam the advertisement/GATT-read paths use — so they unit-test with Qt6::Core only
(`test_gattcommands`). Time-dependent payloads (clock-sync) take the `now` passed to
`gattCommands(now)`; **no** builder reads the wall clock (the injected-clock commitment). The WP6003
read-path trigger, which previously built its clock-sync inline with `QDateTime::currentDateTime()`,
was refactored to call the same pure `clockSyncPayload(now)` builder.

### 3. The ported actions (verified against WatchFlower's `src/devices/*.cpp` **and** `docs/*-ble-api.md`).

| Device | Action | Service | Characteristic | Payload (hex) | Write mode | Handshake | WatchFlower source |
|---|---|---|---|---|---|---|---|
| Flower Care | LedBlink | `00001204…` | `00001a00…` | `fdff` | WithoutResponse | — | device_flowercare.cpp:273 |
| Flower Care | ClearData | `00001206…` | `00001a10…` | `a20000` | WithResponse | RC4 pid 0x0098 | device_flowercare.cpp:365 |
| RoPot | ClearData | `00001206…` | `00001a10…` | `a20000` | WithResponse | RC4 pid 0x015D | device_ropot.cpp:334 |
| Parrot Pot | LedBlink | `39e1fa00…` | `39e1fa07…` | `01` | WithResponse | — | device_parrotpot.cpp:319 |
| Parrot Pot | Watering | `39e1f900…` | `39e1f906…` | `0800` | WithResponse | — | device_parrotpot.cpp:468 |
| ThermoBeacon | LedBlink | `0000ffe0…` | `0000fff5…` | `0400000000` | WithResponse | — | device_thermobeacon.cpp:204 |
| ThermoBeacon | ClearData | `0000ffe0…` | `0000fff5…` | `0200000000` | WithResponse | — | device_thermobeacon.cpp:198 |
| LYWSD02 (clock) | ClockSync | `ebe0ccb0…` | `ebe0ccb7…` | `<u32 LE epoch><i8 tz-hours>` | WithResponse | — | device_hygrotemp_clock.cpp:191 |
| WP6003 | Calibrate | `0000fff0…` | `0000fff1…` | `ad` | WithoutResponse | — | device_wp6003.cpp:132 |
| WP6003 | ClockSync | `0000fff0…` | `0000fff1…` | `aa <yy mm dd HH MM SS>` | WithoutResponse | — | device_wp6003.cpp:142 |

Notes:
- **RoPot has no LED.** It is a TODO stub in WatchFlower's `device_ropot.cpp:249`, so RoPot exposes
  only ClearData. Flower Care & RoPot share one builder
  (`DeviceFlowerCare::commandsFor(productId, withLed)`); only the MiBeacon product id (and the LED)
  differ.
- **The Hygrotemp clock uses the `ebe0ccb0…` / `ebe0ccb7…` (LYWSD02) UUID family**, *not* the
  ThermoBeacon `0000ffe0…` service — an earlier survey conflated the two; the bytes above are the
  verified ones.
- **Clear-history is gated by the RC4 handshake** on current Flower Care / RoPot firmware (same
  handshake as a history read, ADR 0014), so its `GattCommand` carries the handshake spec.
- **Reboot / shutdown were never implemented in WatchFlower.** The base `Device` declares
  `actionReboot`/`actionShutdown` + a `hasReboot()` capability, but **no** concrete device overrides
  them with a write sequence (no override in any `src/devices/*.cpp`). There is no write sequence to
  port, so the `DeviceAction` enum omits them.

### 4. The descriptor types are Qt-Bluetooth-free (`gattprofile.h`, klr_devices).

```cpp
enum class DeviceAction { LedBlink, Watering, Calibrate, ClearData, ClockSync };

struct GattHandshake {                 // MiBeacon RC4 verify, reused from the history fields
    quint16 productId = 0;
    QString service, startCharacteristic, authCharacteristic;
};

struct GattCommand {
    DeviceAction action;
    QString service, characteristic;
    QByteArray payload;                 // exact bytes (ClockSync built from the injected now)
    bool writeWithoutResponse = false;
    std::optional<GattHandshake> handshake; // run before the write when set
};
```

No ack-read field: these actions are fire-and-disconnect in WatchFlower (WP6003's `0xad`/`0x0a`
echo on the RX notify is informational, not awaited). An ack/response read can be added later if a
future action needs it.

### 5. Execution: a `GattCommandSession` (klr_ble), sibling to the read sessions.

`connect → discover → (optional RC4 handshake) → write the payload → disconnect`, modeled on
`GattHistorySession` and reusing its handshake step machine (`mibeacon_auth.h`,
`HandshakeStart/Challenge/Finish`). One command at a time; an inactivity watchdog fails the run if no
write-confirm arrives. Like the other sessions it touches real hardware, so it is **not** in CI
(there is no BLE in CI) — hardware-verified. It is constructed at the composition root when a caller
needs it; nothing in the GUI calls it yet (scope decision).

## Test matrix

| Area | Test | Asserts |
|------|------|---------|
| Byte builders | `test_gattcommands` (pure, ctest) | each device's `gattCommands()` set: action, service, characteristic, exact payload bytes, write mode, handshake (pid + UUIDs) for FC/RoPot clear |
| Clock-sync | `test_gattcommands` | LYWSD02 `clockSyncPayload(now)` = `u32 LE epoch + i8 tz`; WP6003 `clockSyncPayload(now)` = `aa`+BCD-free yy/mm/dd/HH/MM/SS for a fixed `now` |
| Base default | `test_gattcommands` | an advertisement-only device (b-parasite) inherits an empty command set |
| Execution seam | — | `GattCommandSession` is hardware-verified (no BLE in CI), like `GattSession`/`GattHistorySession` |

## Consequences

- Every device action's write sequence is ported into code + this ADR, reconciled with `docs/`,
  unit-tested, and executable.
- Surfacing these actions in the UI (capability-gated buttons + confirmations) remains a later,
  optional phase, deliberately out of scope here.
