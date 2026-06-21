# Klorophylle

**Plant-first, cross-platform plant-monitoring app.** The plant is the first-class object;
Bluetooth sensors are a utility bound to it, so history follows the *plant* — swap or replace a
sensor and keep the timeline.

A C++23 / Qt 6.10+ application that scans BLE plant and environment sensors, stores and charts their
readings, tracks per-plant care status against a journal, and includes an embedded, local-first AI
care assistant.

## Features

- **Plant-first UI** — a plant can exist with no sensor and still be journalled; bind multiple
  sensors to one plant and swap them over time, with history following the plant.
- **Plant catalog** — a searchable species catalog with per-species care thresholds.
- **Care status** — "is my plant healthy?" derived from readings vs. ideal ranges, with light judged
  as Daily Light Integral (DLI).
- **Care journal** — dated entries with photo attachments, per plant and global.
- **Bluetooth sensors** — passive advertisement scanning plus live broadcast values (BTHome, Xiaomi /
  Flower Care, Qingping, ThermoBeacon, b-parasite, and more), on-demand GATT reads, and
  connected-mode history backfill. See [the supported-device list](docs/README.md).
- **History & charts** — bucketed reading history and QtGraphs charts.
- **Settings & units, notifications, export/backup**, plus a WatchFlower `data.db` importer.
- **AI care assistant** — reads a plant's readings, photos, and journal and gives care tips, with
  per-plant and global memory. **Local-first and opt-in:** runs against a local endpoint or a
  bring-your-own remote provider (multiple provider dialects); API keys live in the OS secret store,
  never in app settings. Nothing leaves the device without explicit consent.

## Build

Requires **Qt 6.10+** (with the **Bluetooth / `qtconnectivity`** and **Graphs / `qtgraphs`** modules)
and a **C++23** compiler.

```bash
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/Qt/6.11.1/gcc_64
cmake --build build
./build/src/app/klorophylle            # run the app
```

Or open the top-level `CMakeLists.txt` in Qt Creator with a Qt 6.10+ kit.

> On **Windows**, the Qt backend does not deliver advertisement updates after discovery, so passive
> live values are limited there.

## Layout

| Path | Role |
|---|---|
| `src/core/` | `klr_core` — pure, device-agnostic value types (`Reading`, `Quantity`, `Unit`), the injected `Clock`, formatting, logging. No decoders, no Qt Quick / SQL / Bluetooth. |
| `src/devices/` | `klr_devices` — the device layer: advertisement + GATT decoders **ported from WatchFlower's `AdvertisementParser`**, the snapshot accumulator, and the device registry. Depends on `klr_core` only. |
| `src/ble/` | `klr_ble` — the passive `BleScanner` and GATT sessions (Qt Bluetooth). |
| `src/persistence/` | `klr_persistence` — the repository boundary: SQLite repositories + parity-tested in-memory fakes, transactional migrations. The only place SQL lives. |
| `src/style/` | `klr_style` — the design tokens and the custom Qt Quick Controls style. |
| `src/agent/`, `src/karness/` | `klr_agent` + the `karness` harness — the local-first AI care assistant. |
| `src/gui/` | `klr_gui` — the UI as a library: the `Klorophylle` QML module, per-screen view-models, and the screens on one responsive navigation model. |
| `src/app/` | `klorophylle` — the thin executable (composition root only). |
| `tests/` | the `ctest` suite (67 suites): value types, repositories (SQLite ⇄ in-memory parity), the ported decoders against wire-spec vectors, and the agent harness. |
| `docs/` | the [Architecture Decision Records](docs/adr/) and the per-device BLE wire-protocol specs. |

Library prefix is `klr_`, matching the `klr` namespace. The "why" behind each subsystem is recorded
in the [ADRs](docs/adr/).

## License

GPL v3 — see [`LICENSE.md`](LICENSE.md).

## Acknowledgements

Klorophylle is inspired by **WatchFlower** by Emeric Grange (GPL v3) —
<https://github.com/emericg/WatchFlower>. Some of its Bluetooth sensor-decoding code is derived from
WatchFlower, and some data files (e.g. the plant catalog) are reused from it. With thanks to the
WatchFlower project and its contributors.
