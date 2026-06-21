# ADR 0021 — Per-plant agent memory

**Status:** accepted · **Builds on:** ADR 0020 (dual-timestamp journal),
ADR 0019 (the agent domain tools / `agenttools`, `AgentViewModel`/`ConfirmingTool`), ADR 0004
(repository boundary), ADR 0010 (backup tokens), ADR 0013 (cyan `colorAI` AI accent).

The agent can read a plant's data and journal and propose a confirmed journal entry, but it
**forgets everything between conversations** — each turn starts from the system prompt + roster +
the new message. This milestone gives it **durable per-plant memory**: facts it should carry forward
("waters lightly", "south-facing window", "sensitive to repotting"). Per ADR 0019 decision 11 this is
**not** a new store — memory is a special **journal entry**, so it inherits transparency (the user
sees it in the journal), edit/delete, and sync-readiness (the journal is change-logged) for free,
and it sits on the dual-timestamp model ADR 0020 just landed: an agent rewrite **floats** the entry
to "now"; a user edit never reorders it.

This milestone is **per-plant** memory only. Global memory + the global journal are a later milestone (its own ADR).
It adds **no schema change** — it builds on ADR 0020's schema v6.

## Decisions

1. **`JournalEntryKind::Memory = 6` — the next free pinned value.** The klorophylle enum runs
   `Note=0`…`Observation=5`; append `Memory = 6` (pinned, never renumbered). `backuptokens::toToken`
   gains a `"Memory"` case and `fromToken` bumps its reverse-scan bound to `Memory + 1` (so backups
   round-trip it); `journalKindLabel`/`journalKindLabels` gain a localized "Memory" **for rendering**
   existing entries. Because `kind` is an `INTEGER` column, the new value needs **no migration**.

2. **Exactly one Memory entry per plant; the agent authors it, the user only curates.** The single
   `Memory` entry is the plant's living memory blob. Users **cannot create** one: `AppContext::
   addJournalEntry` already rejects `kind > Observation`, so `Memory` (6) is uncreatable through it,
   and the QML create-kind picker is fed a **new creatable-kinds list that omits Memory** (the full
   `journalKindLabels` stays for *rendering*). Users **can edit/delete** an existing Memory entry
   through the normal ADR 0020 paths — and a user edit bumps only `editedAt` (it does **not** reorder
   the entry), exactly as for any entry, so no special-casing is needed on the user side. The
   "one per plant" guarantee is a **tool/repository invariant** (decision 3), not a DB constraint.

3. **`set_plant_memory(plant_id, text)` — one narrow, *unconfirmed* write tool (`klr_agent`).** A
   plain `karness::ITool` in the `agenttools` TU, sharing the `resolvePlant` helper and the
   injected clock. `text` is the **complete** new memory (the model reads the old memory first and
   rewrites the whole blob — a living document, not an append log). The tool:
   - finds the plant's single `Memory` entry (via `forPlant`, filter `kind == Memory`; newest if more
     than one somehow exists);
   - **if present** — an **in-place rewrite**: keep the entry's **stable id**, set `note = text`, and
     bump **both** `timestamp` *and* `editedAt` to `now`, **floating** it to the top of the timeline.
     This is the **sole exception** to ADR 0020's entry-date immutability, confined to Memory entries
     reached through this tool;
   - **if absent** — create one: `kind = Memory`, `timestamp = now`, `editedAt = nullopt` (a creation,
     not yet an edit; its fresh entry date already floats it).

   It returns a ready future; a blank/unknown `plant_id` or empty `text` is an `isError` outcome the
   model recovers from, never a turn failure (the agent-tool convention). Crucially it is **not** wrapped in
   `ConfirmingTool` — unlike `add_journal_entry`, a memory write executes **without** user
   confirmation. The oversight is decision 11's triad: it is **visible** in the journal, **editable/
   deletable** by the user, and the capability is **narrow** (set-my-memory only — never a general
   edit/delete-any-entry tool), and memory only ever **feeds future advice**, it never auto-executes.

4. **The read path is left open, defaulting to the existing journal read.** Per the decision to
   settle the read shape by what works best for the agent: this milestone ships **no dedicated read tool** — the
   `Memory` entry already surfaces through `read_plant_journal` (kind `"Memory"`), so the model reads
   its memory by reading the journal it already reads. A dedicated `read_plant_memory` is added **only
   if** in-practice testing shows the model recalls better with a focused, cheaper read (e.g. it
   ignores the Memory entry buried in a long journal). Recorded as a deliberately empirical follow-up,
   not a pinned tool.

5. **UI: render Memory with the cyan AI accent; hide its creation.** The journal entry delegate draws
   a `Memory` entry with the `colorAI` cyan accent (the AI-authored marker, consistent with
   `PulsingNode`/ADR 0013) and shows its edit date (the ADR 0020 `EditedAtRole`); user edit/delete
   stay available. The create-kind picker omits Memory (decision 2). No judgment/format logic enters
   QML — labels/colours come from C++ (`journalKindLabel`, `Theme.colorAI`).

6. **Agent wiring + prompt.** `AgentViewModel` registers `set_plant_memory` (the plain, unconfirmed
   tool) on `AgentSession` alongside the other domain tools when `agentToolsEnabled`; `ConfirmingTool` continues
   to wrap **only** `add_journal_entry`. `buildInstructions()` (the stable, cacheable system prompt —
   ADR 0019) gains, in its tools-on branch, guidance to **remember durable per-plant facts with
   `set_plant_memory` and to consult them** (it already instructs reading the journal). The
   instructions stay clock-free and stable, so the cacheable prefix is unaffected.

## Consequences

- The agent persists and recalls durable per-plant facts across conversations and app restarts; each
  fact is one journal entry the user can read, edit, or delete, positioned at the agent's last
  revision.
- This milestone introduces the **one** code path that re-dates a journal entry (the Memory float); every other
  edit — including a *user* editing a Memory entry — leaves the entry date alone (ADR 0020), so the
  timeline stays stable except where the agent deliberately refreshes its memory.
- Memory rides **sync** (the journal is change-logged; an in-place rewrite is one `update` row on a
  stable id) and **backup/restore** (decision 1's token) with no extra work.
- **No schema change** — this is purely a new kind + a tool + UI on top of ADR 0020's v6.
- The unconfirmed write is a deliberate, bounded relaxation of the confirm-before-write rule, scoped
  to the narrow memory capability; the prompt-injection surface stays the journal-visibility + narrow
  tool posture of decision 11.

## Implementation order (one commit each, `ctest` green at each)

1. `JournalEntryKind::Memory = 6` + `backuptokens` (`toToken` case, `fromToken` bound) + `journalformat`
   labels (+ `test_backuptokens` Memory round-trip; `test_journalformat` if present).
2. `set_plant_memory` in `agenttools` (find → in-place rewrite bumping both dates → else create;
   `resolvePlant`; injected clock; the one-entry invariant) (+ `test_agenttools`: create-then-rewrite
   keeps the id and floats both dates; never a second entry; blank/unknown id → `isError`; the entry
   reads back through `read_plant_journal`).
3. `AgentViewModel` registers the unconfirmed tool + `buildInstructions()` tools-on guidance
   (+ `test_agentviewmodel`: the tool is registered and *not* gated by `ConfirmingTool`; prompt
   mentions memory).
4. UI — a creatable-kinds list on `AppContext` that omits Memory; the journal delegate's `colorAI`
   Memory rendering + edit-date line (+ `test_appcontext` for the creatable-kinds exclusion;
   qmllint/cachegen for the delegate).
5. *(Conditional)* a dedicated `read_plant_memory` tool **iff** decision 4's evaluation calls for it
   (+ `test_agenttools`).
6. This ADR.

## Tests

`test_backuptokens` (Memory token ⇄ value), `test_agenttools` (`set_plant_memory`: first call creates
one entry at `now`; a second rewrites **in place** — same id, both dates bumped, still exactly one
entry; full-blob replace; `isError` on blank/unknown plant; the entry surfaces via
`read_plant_journal`), `test_agentviewmodel` (tool registered, unconfirmed, prompt names it),
`test_appcontext` (Memory absent from the creatable-kinds list; a user edit of a Memory entry bumps
only `editedAt` — already covered by ADR 0020's `editJournalEntry` test, re-asserted for the Memory
kind). The journal QML delegate stays qmllint/cachegen-only by design.

## Out of scope / follow-ups

- **Global memory + the global journal** — the global-memory milestone (ADR 0022): plant-less journal entries
  + `globalEntries()` + a global-journal UI surface + `set_global_memory`/`read_global_memory`.
- **A targeted `IJournalRepository::memoryFor(plant)` query** — `forPlant` + filter is fine at this
  scale; add a focused query only if the journal grows large enough to matter.
- **A confirmation gate on memory writes** — deliberately *not* added (decision 3); the wider-blast-
  radius gate question belongs to global memory.
- **A dedicated read tool** — decision 4, only if evaluation calls for it.
