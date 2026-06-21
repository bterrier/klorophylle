// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QUuid>
#include <QtCore/QString>

// ONE identity vocabulary — see docs/adr/0001-identity-vocabulary.md.
//   PlantId        — app-minted, stable identity of a plant. History keys on this,
//                    so it survives sensor swaps (plant-first, goal #1).
//   SensorId       — app-minted identity of a physical sensor (bindings / sync).
//   JournalEntryId — app-minted identity of a single journal entry.
// DeviceHandle (platform BLE MAC/UUID, dedup only) and RegistryKey (device-model
// factory key) arrive with the device layer.
//
// Every syncable entity carries an app-minted UUIDv7 identity, independent of any
// SQLite rowid (sync-readiness). v7 is time-ordered,
// giving better index locality than v4.
namespace klr {

struct PlantId {
    QUuid value;
    static PlantId generate() { return { QUuid::createUuidV7() }; }
    bool operator==(const PlantId &) const = default;
    QString toString() const { return value.toString(QUuid::WithoutBraces); }
};

struct SensorId {
    QUuid value;
    static SensorId generate() { return { QUuid::createUuidV7() }; }
    bool operator==(const SensorId &) const = default;
    QString toString() const { return value.toString(QUuid::WithoutBraces); }
};

struct JournalEntryId {
    QUuid value;
    static JournalEntryId generate() { return { QUuid::createUuidV7() }; }
    bool operator==(const JournalEntryId &) const = default;
    QString toString() const { return value.toString(QUuid::WithoutBraces); }
};

// Identity of a single journal attachment (a photo, for now). Keyed to its journal
// entry, cascade-deleted with it (ADR 0024). Syncable, so a UUIDv7 like the rest.
struct AttachmentId {
    QUuid value;
    static AttachmentId generate() { return { QUuid::createUuidV7() }; }
    bool operator==(const AttachmentId &) const = default;
    QString toString() const { return value.toString(QUuid::WithoutBraces); }
};

// AI agent transcript identities. Device-local — agent conversations are NOT
// change-logged (like sensor_sync_state), so these never sync, but they still carry a
// stable UUIDv7 identity for ordering + foreign keys.
struct ConversationId {
    QUuid value;
    static ConversationId generate() { return { QUuid::createUuidV7() }; }
    bool operator==(const ConversationId &) const = default;
    QString toString() const { return value.toString(QUuid::WithoutBraces); }
};

struct AgentMessageId {
    QUuid value;
    static AgentMessageId generate() { return { QUuid::createUuidV7() }; }
    bool operator==(const AgentMessageId &) const = default;
    QString toString() const { return value.toString(QUuid::WithoutBraces); }
};

} // namespace klr
