// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QString>
#include <QtCore/QDateTime>

#include <optional>

// A single entry in a plant's care journal. A plant can be journalled with no
// sensor at all (plant-first, goal #1). A plain value type, like Plant/Reading.
namespace klr {

// Persisted as the INTEGER `kind` column of journal_entries (schema.cpp) and carried into the
// change_log payload. These integer values are a STABLE WIRE FORMAT: never reorder, renumber, or
// reuse a value — only append new kinds with the next free integer. The legacy importer and the
// QML range check (appcontext.cpp) rely on the contiguous [Note..Observation] range.
//
// A photo is deliberately NOT a kind. Attachments (photos now, media later) are an orthogonal
// evidence axis on ANY entry — see IAttachmentRepository (built, ADR 0024).
enum class JournalEntryKind {
    Note        = 0,
    Watering    = 1,
    Fertilizing = 2,
    Repotting   = 3,
    Pruning     = 4,
    Observation = 5,
    // Agent-authored per-plant memory (ADR 0021). Outside the creatable [Note..Observation]
    // range on purpose: the user can edit/delete it but cannot create one — only the agent's
    // set_plant_memory tool writes it (exactly one per plant). Rendered with the cyan AI accent.
    Memory      = 6,
};

struct JournalEntry {
    JournalEntryId id;
    // The plant this entry belongs to, or nullopt for a GLOBAL (plant-less) entry — the global
    // journal that holds user-wide agent memory + global notes (ADR 0022). nullopt is the
    // honest "no plant" value, not a sentinel id; persisted as a NULL plant_id.
    std::optional<PlantId> plant;
    // The ENTRY DATE: the creation instant and the journal's sort key (ORDER BY ts_utc). Immutable
    // after creation — a user edit moves only editedAt, never this (ADR 0020). UTC.
    QDateTime timestamp;          // when the care event happened (UTC)
    JournalEntryKind kind { JournalEntryKind::Note };
    QString note;
    // The LAST-EDITED instant (UTC). nullopt == never edited — NOT "edited at the creation time"
    // (the no-sentinel rule). Not a sort key. Set by the business edge on edit (agent rewrites
    // also bump it).
    std::optional<QDateTime> editedAt;

    bool operator==(const JournalEntry &) const = default;
};

} // namespace klr
