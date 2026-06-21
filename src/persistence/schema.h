// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "migrationrunner.h"

#include <vector>

// The schema, as an ordered migration list. Every syncable entity has a UUIDv7 text
// primary key (sync-readiness), and a transactional
// change_log exists from day one (the reducer/sync is deferred).
//   v1 — plants, journal_entries, change_log, app_meta (ADR 0004).
//   v2 — sensors, plant_sensor_bindings, readings: the plant<->sensor join, with
//        readings keyed on sensor_id and the plant derived through bindings (ADR 0005).
//   v3 — care_thresholds: per-plant ideal ranges for the care-status judgment (ADR 0009).
//   v4 — sensor_sync_state: per-sensor last GATT history sync, device-local (ADR 0014).
//   v5 — agent_conversations, agent_messages: AI-agent transcripts, device-local (ADR 0019).
//   v6 — journal_entries.ts_edited: dual-timestamp entries, nullable last-edited instant (ADR 0020).
//   v7 — journal_entries.plant_id made NULLABLE: plant-less (global) entries for the global journal
//        / user-wide agent memory (ADR 0022). Table rebuilt (SQLite can't drop NOT NULL in place).
//   v8 — attachments: file-backed photos keyed to a journal entry, cascade-deleted with it (ADR 0024).
namespace klr {

inline constexpr int kSchemaVersion = 8;

std::vector<Migration> baselineMigrations();

} // namespace klr
