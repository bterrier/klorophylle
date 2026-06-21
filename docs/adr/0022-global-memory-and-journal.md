# ADR 0022 — Global agent memory + the global journal

**Status:** accepted · **Builds on:** ADR 0021 (per-plant memory / the
`Memory` kind + tool shape), ADR 0020 (dual-timestamp journal), ADR 0019 (the agent domain tools /
`agenttools`, `AgentViewModel`/`ConfirmingTool`), ADR 0004 (repository boundary), ADR 0013 (cyan
`colorAI` AI accent).

Since the per-plant memory milestone (ADR 0021) the agent carries **per-plant** durable memory — a single `Memory` journal entry
on a plant. This is the last memory milestone: **user-wide (global) memory**, facts that colour advice
for *every* plant ("travels often", "hard tap water", "lives in a dry climate"), plus a
**global journal** UI surface where the user reads and curates it.

Per ADR 0019 decision 11, global memory is **not a new store** — it is a `Memory` entry on a
**plant-less journal**, inheriting transparency, edit/delete, sync-readiness and the dual-timestamp
float (ADR 0020) exactly as per-plant memory does. The one thing per-plant memory did not need: a
journal entry whose plant scope is **optional**.

## Decisions

1. **A journal entry's plant scope becomes optional — `nullopt` == a global (plant-less) entry.**
   `JournalEntry::plant` changes from `PlantId` to `std::optional<PlantId>` (the honest model, no
   sentinel). Schema **v7** rebuilds `journal_entries` to make `plant_id` **nullable**
   (SQLite cannot drop a `NOT NULL` in place). The FK `REFERENCES plants(id) ON DELETE CASCADE` is
   kept: a NULL FK is never cascaded, so global entries **survive a plant delete**. `IJournalRepository`
   gains `globalEntries()` (plant-less entries, newest-first) alongside `forPlant`.

2. **`set_global_memory(text)` — one narrow, *unconfirmed* write tool, mirroring per-plant memory.** A plain
   `karness::ITool` in the `agenttools` TU (no plants repo — global has no `plant_id`). `text` is the
   **complete** new memory (read-then-rewrite, a living document). It finds the single global `Memory`
   entry via `globalEntries()` filtered `kind == Memory`; **if present** an in-place rewrite (keep the
   stable id, set `note`, bump **both** `timestamp` and `editedAt` to now — floating it); **if absent**
   create one (`plant = nullopt`, `editedAt = nullopt`). Blank `text` → `isError`. Like
   `set_plant_memory` it is **unconfirmed** (not wrapped in `ConfirmingTool`): oversight is decision
   11's triad — **visible** in the global journal, **editable/deletable** by the user, and the
   capability is **narrow** (set-my-global-memory only). Global memory has the wider blast radius (it
   colours advice for every plant); a confirmation gate is the recognized escalation **if wanted** —
   deliberately **not** added here, matching the per-plant posture.

3. **`read_global_memory()` — a dedicated read tool (unlike per-plant memory).** Per-plant memory folded the read into the
   existing `read_plant_journal` because the per-plant Memory entry already surfaces there. Global
   memory has **no plant journal the agent already reads**, so it needs a read path of its own (ADR
   0019 decision 11). `read_global_memory` (no args) returns the global `Memory` entry's text, or an
   "empty" message when none exists.

4. **A global-journal UI surface, reached from the AI assistant screen.** The global journal renders
   plant-less entries (newest-first) with edit/delete — curatable per the exit criterion — and renders
   the `Memory` entry with the cyan `colorAI` accent + edit date, exactly as the per-plant journal does
   (ADR 0021 decision 5). It is a full-page route (`Route::GlobalJournal`) **pushed from the
   `AIInsightsScreen`** (global memory is agent territory; this keeps the nav rail uncluttered).

5. **The user can create global notes too — global creatable kinds = `Note` only.** Unlike a plant's
   journal (creatable `[Note..Observation]`), the care kinds (Watering/Fertilizing/Repotting/Pruning)
   and Observation are inherently plant-scoped, and `Memory` is agent-authored. So a global entry the
   *user* creates is a `Note`. Exposed as a `globalCreatableJournalKinds` list (just `Note`), keeping
   the existing "picker index == kind value" mapping valid (index 0 → `Note`). A user edit of a global
   Memory entry moves only `editedAt` (ADR 0020) and can never convert an entry into an uncreatable kind.

6. **Agent wiring + prompt.** `AgentViewModel` registers `set_global_memory` and `read_global_memory`
   (both unconfirmed) on `AgentSession` alongside the other domain tools when `agentToolsEnabled`;
   `ConfirmingTool` continues to wrap **only** `add_journal_entry`. `buildInstructions()` (the stable,
   cacheable system prompt — ADR 0019) gains, in its tools-on branch, guidance to remember durable
   **user-wide** facts with `set_global_memory` and to consult global memory with `read_global_memory`.
   The instructions stay clock-free and stable, so the cacheable prefix is unaffected.

## Consequences

- The agent persists and recalls durable **user-wide** facts across conversations and app restarts;
  each fact is one global journal entry the user can read, edit, or delete.
- Global memory rides **sync** (the journal is change-logged) and **backup/restore** — but the
  backup serializer, which walked plants only, must now **also serialize `globalEntries()`** (else a
  global entry is silently dropped); the importer parses a missing/null `plant` as `nullopt`.
- Making `plant` optional is a load-bearing type change touching every journal reader/writer (both
  repositories, the change-log payload, backup serializer/importer, the duplicator/legacy importer,
  the journal list model). The `Memory`-float exception (ADR 0021) and the user-edit-moves-only-edit
  -date invariant (ADR 0020) carry over unchanged to the global scope.
- This is the **only** schema change for global memory — a nullable column + one repository query on top
  of ADR 0020's v6 and ADR 0021's `Memory` kind. No new entity.

## Implementation order (one commit each, `ctest` green at each)

1. This ADR (docs-only).
2. Persistence — `JournalEntry::plant` → `std::optional<PlantId>`; `IJournalRepository::globalEntries()`;
   schema **v7** (rebuild `journal_entries` with nullable `plant_id`); both repositories
   (`Sqlite*`/`InMemory*`) map NULL ⇄ `nullopt` and implement `globalEntries()`
   (+ `test_journalrepository` global-entries parity; migration v6→v7 preserves rows + accepts NULL).
3. Backup/restore + duplicator carry the optional plant (serializer also walks `globalEntries()`;
   importer parses absent `plant` → `nullopt`) (+ backup round-trip of a global entry).
4. `set_global_memory` + `read_global_memory` in `agenttools` (mirror `SetPlantMemoryTool`; no plants
   repo; injected clock; the one-entry invariant) (+ `test_agenttools`).
5. `AgentViewModel` registers both unconfirmed tools + `buildInstructions()` tools-on guidance
   (+ `test_agentviewmodel`).
6. `AppContext` + the journal model: a global-scoped model, `globalCreatableJournalKinds` (`Note`),
   `addGlobalJournalEntry`/`editGlobalJournalEntry`/`removeGlobalJournalEntry` (+ `test_appcontext`).
7. UI — `Route::GlobalJournal`; `GlobalJournalScreen.qml` (reusing the per-plant journal delegate); the
   `AIInsightsScreen` affordance (qmllint/cachegen).
8. Set this ADR's status accepted.

## Tests

`test_journalrepository` (`globalEntries()` newest-first; excluded from `forPlant` and vice-versa; a
global entry survives plant deletion), migration test (v6→v7 preserves rows incl. `ts_edited`; a
`plant_id IS NULL` insert succeeds), `test_agenttools` (`set_global_memory`: first call creates one
global Memory entry at `now`, `editedAt` nullopt; a second rewrites **in place** — same id, both dates
bumped, still exactly one; blank text → `isError`; `read_global_memory` surfaces it and reports the
empty case), `test_agentviewmodel` (both tools registered, unconfirmed, prompt names global memory),
`test_appcontext` (`globalCreatableJournalKinds` omits Memory/care kinds; `addGlobalJournalEntry`
writes a plant-less entry; a user edit of a global Memory entry bumps only `editedAt`), backup
round-trip of a global entry. The global QML delegate stays qmllint/cachegen-only by design.

## Out of scope / follow-ups

- **A confirmation gate on global memory writes** — deliberately not added (decision 2); revisit only
  if the wider blast radius proves a problem in practice.
- **Richer global entry kinds** — this milestone lets the user create only global `Note`s; broadening the global
  creatable set is a later UI decision.
- **A targeted `IJournalRepository::globalMemory()` query** — `globalEntries()` + filter is fine at
  this scale; add a focused query only if the global journal grows large.
