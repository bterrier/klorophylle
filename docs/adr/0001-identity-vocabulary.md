# ADR 0001 — One identity vocabulary

**Status:** accepted

The early design used three names (`DeviceId` / `DeviceIdentity` / `SensorId`) and two meanings of
"stable key". We fix exactly one vocabulary and use it everywhere:

| Type | Meaning | Used by | Notes |
|---|---|---|---|
| **`PlantId`** | App-minted, stable identity of a *plant* | journal, readings, bindings, sync | **History follows the plant** → survives sensor swaps (goal #1). |
| **`SensorId`** | App-minted, stable identity of a *physical sensor* | bindings, sync | Independent of the BLE handle. |
| **`JournalEntryId`** | App-minted, stable identity of a *journal entry* | journal, sync | Added with the plant-journal persistence (Milestone B). |
| **`DeviceHandle` `{kind,value}`** | Platform BLE handle (MAC / CoreBluetooth UUID) | dedup, reconnect **only** | Arrives with the device layer. Never an entity identity. |
| **`RegistryKey`** | Device-*model* key → factory + capabilities | `DeviceRegistry` | One key for discovery **and** DB-restore (kills the two drifting switches). Not an instance id. |

**Decisions**

- Every syncable entity carries an **app-minted UUID identity independent of any SQLite rowid**
  (sync-readiness).
- Identities are minted with **`QUuid::createUuidV7()`** (time-ordered → better index locality;
  verified available in Qt 6.11). *(Superseding the early `createUuid()` v4 stopgap, adopted with
  the first SQLite persistence — Milestone B.)*
- `PlantId` / `SensorId` / `JournalEntryId` are defined in `src/persistence/ids.h`. `DeviceHandle` /
  `RegistryKey` arrive with `klr_devices`.
