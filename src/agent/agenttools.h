// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "itool.h" // karness::ITool, ToolOutcome, ToolSpec (+ QFuture/QJsonObject)

// The agent's domain tools (docs/adr/0019 decision 5): read-only views over the
// repositories plus one confirmed write. Each is a small karness::ITool with no SQL —
// it calls a repository seam and renders a compact text result, tested on the in-memory
// fakes. Tools are plant-id addressed; list_plants surfaces the ids the others take.
//
// Synchronous by nature: the repositories answer in-process, so invoke() returns a ready
// future. The async ITool contract still holds — the user-confirmation decorator wraps
// add_journal_entry without any of these tools knowing about confirmation (decision 8).
//
// Lives in klr_agent (karness + klr_core + klr_persistence), never klr_gui: the care
// status uses the pure klr_core helpers (carestatus.h), not the GUI orchestrator.
namespace klr {

class IPlantRepository;
class IJournalRepository;
class IAttachmentRepository;
class IAttachmentFileStore;
class IBindingRepository;
class IReadingRepository;
class ICareThresholdRepository;
class IWebFetcher;
class Clock;

// list_plants — no arguments. Lists every tracked plant with its id (so later tools can
// reference it), display name, species, and days tracked.
class ListPlantsTool final : public karness::ITool {
public:
    ListPlantsTool(const IPlantRepository &plants, const Clock &clock);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    const IPlantRepository &m_plants;
    const Clock &m_clock;
};

// read_plant_journal — args { plant_id: string, limit?: int }. The plant's most recent
// care-journal entries (newest first), each as timestamp · kind · note.
class ReadPlantJournalTool final : public karness::ITool {
public:
    ReadPlantJournalTool(const IPlantRepository &plants, const IJournalRepository &journal);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    const IPlantRepository &m_plants;
    const IJournalRepository &m_journal;
};

// read_plant_data — args { plant_id: string, window_days?: int }. The plant's current
// sensor readings, each judged against its care threshold (ideal / below / above / none),
// plus an optional min·avg·max summary over the last window_days. Reads through the binding
// window so history follows the plant across sensor swaps (ADR 0005). Care status uses the
// pure klr_core helpers (carestatus.h), never the klr_gui orchestrator.
class ReadPlantDataTool final : public karness::ITool {
public:
    ReadPlantDataTool(const IPlantRepository &plants, const IBindingRepository &bindings,
                      const IReadingRepository &readings, const ICareThresholdRepository &thresholds,
                      const Clock &clock);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    const IPlantRepository &m_plants;
    const IBindingRepository &m_bindings;
    const IReadingRepository &m_readings;
    const ICareThresholdRepository &m_thresholds;
    const Clock &m_clock;
};

// add_journal_entry — args { plant_id: string, note: string, kind?: string }. The single
// WRITE tool (docs/adr/0019 decision 5): appends a care-journal entry timestamped from the
// injected Clock. This ships the bare write; user confirmation is layered on as a
// decorator tool (decision 8) and the "agent-authored" marking arrives with the
// AgentRepository migration — this tool stays confirmation-agnostic.
class AddJournalEntryTool final : public karness::ITool {
public:
    AddJournalEntryTool(const IPlantRepository &plants, IJournalRepository &journal,
                        const Clock &clock);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    const IPlantRepository &m_plants;
    IJournalRepository &m_journal;
    const Clock &m_clock;
};

// set_plant_memory — args { plant_id: string, text: string }. The agent's durable per-plant
// memory (docs/adr/0021): the COMPLETE memory blob (a living document the model rewrites
// whole, not an append log). Exactly one Memory entry per plant, a tool invariant: it finds the
// plant's single JournalEntryKind::Memory entry and rewrites it IN PLACE — same stable id, both
// timestamp AND editedAt bumped to now, FLOATING it to the top of the timeline (the sole exception
// to ADR 0020's entry-date immutability, confined to Memory entries reached through this tool); if
// none exists it creates one (editedAt nullopt — a creation, not an edit). Unlike add_journal_entry
// it is NOT wrapped in a confirmation decorator (decision 3): the oversight is that memory is
// visible + user-editable in the journal, the capability is narrow, and it only ever feeds future
// advice. Same klr_agent home + injected Clock as the other tools.
class SetPlantMemoryTool final : public karness::ITool {
public:
    SetPlantMemoryTool(const IPlantRepository &plants, IJournalRepository &journal,
                       const Clock &clock);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    const IPlantRepository &m_plants;
    IJournalRepository &m_journal;
    const Clock &m_clock;
};

// set_global_memory — args { text: string }. The agent's durable USER-WIDE memory (docs/adr/0022):
// the global counterpart of set_plant_memory, with no plant_id (it writes the single
// JournalEntryKind::Memory entry on the GLOBAL, plant-less journal). Same read-then-rewrite-whole
// contract, same in-place rewrite (stable id, both dates bumped, floats to now) / create-if-absent
// behaviour, and same UNconfirmed posture (decision 2): oversight is the global journal's visibility +
// user edit/delete + the narrow capability. Needs no plant repository.
class SetGlobalMemoryTool final : public karness::ITool {
public:
    SetGlobalMemoryTool(IJournalRepository &journal, const Clock &clock);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    IJournalRepository &m_journal;
    const Clock &m_clock;
};

// read_global_memory — no arguments. Returns the agent's user-wide memory (the global Memory entry's
// text), or an "empty" message when none has been saved. A dedicated read tool (unlike the per-plant
// memory, which folded its read into read_plant_journal): there is no global plant-journal the agent
// already reads, so global memory needs a read path of its own (docs/adr/0022 decision 3).
class ReadGlobalMemoryTool final : public karness::ITool {
public:
    explicit ReadGlobalMemoryTool(const IJournalRepository &journal);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    const IJournalRepository &m_journal;
};

// read_plant_photo — args { plant_id: string, limit?: int }. Returns the plant's most-recent journal
// photos (docs/adr/0025) as image content for a vision model: each photo is a short text line
// (date · caption) followed by the image bytes, newest entries first, capped at `limit` (default 4 —
// vision payloads are large). Read-only; registered only when vision is enabled (the view-model gates
// on ModelCaps.vision), so an ImageBlock only ever reaches a dialect with vision on. A missing file
// (e.g. a restored-without-files backup) is skipped. Reads the metadata repo + the file-bytes store.
class ReadPlantPhotoTool final : public karness::ITool {
public:
    ReadPlantPhotoTool(const IPlantRepository &plants, const IJournalRepository &journal,
                       const IAttachmentRepository &attachments,
                       const IAttachmentFileStore &fileStore);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    const IPlantRepository &m_plants;
    const IJournalRepository &m_journal;
    const IAttachmentRepository &m_attachments;
    const IAttachmentFileStore &m_fileStore;
};

// read_online_plant_db — args { query: string, source?: "wikipedia" | "wikispecies" }. The web
// tool (docs/adr/0023): fetch a reputable plant-encyclopedia page for `query` (the species /
// botanical name) and return it as readable plain text. The model picks a SOURCE by name, never a
// URL — the host owns the allowlist (Wikipedia + Wikispecies) and builds the URL from WatchFlower's
// species->slug rule (decisions 1–2). The only async tool: it returns the fetcher's future mapped
// through htmlToText + truncation (decision 3). Untrusted input, so it is read-only + allowlisted +
// gated by an opt-in setting (decision 6) — registered only when the user enables web lookups.
class ReadOnlinePlantDbTool final : public karness::ITool {
public:
    explicit ReadOnlinePlantDbTool(IWebFetcher &fetcher);

    [[nodiscard]] karness::ToolSpec spec() const override;
    [[nodiscard]] QFuture<karness::ToolOutcome> invoke(const QJsonObject &args) override;

private:
    IWebFetcher &m_fetcher;
};

} // namespace klr
