# ADR 0019 — AI agent: karness harness + klr_agent, provider dialects, and the build roadmap

**Status:** accepted (roadmap; each milestone gets its own implementation pass) · **Builds on:** ADR 0001 (vocabulary), ADR 0002
(injected QML singletons), ADR 0004 (persistence/repository boundary).

This ADR opens the AI-agent work. The direction: build **our own agent harness** as a library extractable one
day as a standalone C++ library; support **BYOK network LLMs and local LLMs** (llama.cpp et al.);
support **both the Chat Completions and the Responses API** styles; support **all major provider
APIs**; expose **domain tools** to the agent (list plants, read plant data, read journal, read
photo, save notes); keep **web tools minimal** (a reputable-plant-encyclopedia reader at most);
support **reasoning**. One reframing simplifies everything: **mobile is postponed, so the AI agent
targets the Linux desktop product** — the device-gated platform providers
(AICore, Apple Foundation Models, LiteRT) are out of scope until mobile support lands, and what remains
(network BYOK + llama.cpp) is the tractable part. This ADR is the authoritative AI agent design —
naming, the backend table, and phase mapping — with its goal statement, compliance rules,
and risk register.

## Decisions

1. **Two libraries: `karness` (generic harness) + `klr_agent` (klorophylle integration).**
   `karness` deliberately does **not** carry the `klr_` prefix: it must know nothing about plants,
   repositories, or QML. Allowed dependencies: **QtCore + QtNetwork + C++23 only** (errors via
   `std::expected`, matching the codebase) — no QtQml/Quick, no QtSql, no `klr_*`. It owns the
   message/thread model, tool specs, provider dialects, SSE parsing, the bounded agent loop, and
   streaming. `klr_agent` sits in the graph above persistence (`klr_core` → `klr_persistence` →
   `klr_agent` → `klr_gui`) and owns `ContextBuilder`, the domain tools, `AgentRepository`
   (transcript persistence), consent, and key storage. **CI gate:** `karness` must configure and
   build **standalone** (its own `CMakeLists.txt`, nothing but Qt on the prefix path) — the same
   link-audit spirit as the probe target; this keeps "extractable one day" mechanically true.

2. **A canonical content-block message model is the load-bearing type.** An earlier
   `ChatMessage { role, text }` sketch is too thin for Responses/Anthropic/reasoning. Instead:
   `Message { Role role; QList<ContentBlock> blocks; }` with
   `ContentBlock = Text | Reasoning{ text, providerOpaque } | ToolCall{ id, name, argsJson } |
   ToolResult{ callId, QList<ContentBlock> } | Image{ bytes, mime }`.
   *(In the harness-skeleton milestone: `ToolResult` carries `QList<ContentPart>` with `ContentPart = Text | Image` —
   a deliberate narrowing that avoids a recursive variant and matches provider reality; no provider
   accepts reasoning or nested tool calls inside a tool result.)*
   The `providerOpaque` blob is essential: Anthropic requires thinking blocks (with signatures)
   echoed back **verbatim** inside a tool loop, and OpenAI Responses has reasoning items with
   `encrypted_content` for stateless replay — the canonical model must round-trip these opaquely or
   reasoning + tool-calling breaks on exactly the providers where it matters. `ToolResult` carrying
   blocks (not just text) is what lets a tool return an image where the dialect allows it.
   `ModelCaps { toolCalling, vision, reasoning, ctxTokens }` gates features per backend.

3. **"All major providers" = four dialects, not N integrations.** Each dialect is a pure request
   encoder + stream-event decoder behind one `Provider` interface (streaming via
   `QPromise<StreamEvent>::addResult`): **(a) OpenAI Chat Completions** — doubles as the
   compat dialect for Ollama, llama.cpp `server`, LM Studio, vLLM, Groq, DeepSeek, Mistral,
   OpenRouter, … (one base URL + token); **(b) OpenAI Responses**; **(c) Anthropic Messages**;
   **(d) Gemini `generateContent`**. Typed stream events (`TextDelta`, `ReasoningDelta`,
   `ToolCallStart`/`ToolCallArgsDelta`, `Done`, `Error`), one shared SSE parser. **Conformance is
   fixture-tested:** real captured streams replayed through a local mock HTTP server — no network
   in ctest, deterministic, and each provider's quirks are pinned the day they were captured.

4. **Local-first ships via the *server* path first; in-process llama.cpp is last and spike-gated.**
   "llama.cpp support" is two different lifts. Pointing dialect (a) at `http://localhost:11434`
   (Ollama / `llama-server` / LM Studio) gives local-first privacy at the *first* usable milestone
   with zero vendoring. In-process llama.cpp (vendored lib, GGUF `ModelManager`, grammar-constrained
   decoding) is the heavy lift the risk deep-dive scoped — free-form small-model output is ≈27%
   valid JSON vs ≈90% grammar-constrained, grammar-loop bugs are real — so it lands **last** (the in-process llama.cpp milestone),
   behind the deep-dive's go/no-go spike (≥85% parse / ≥70% tool-name accuracy, Qwen-3B-class
   Q4_K_M, no loop escaping the iteration guard). If the spike no-goes, local-first still exists
   via the server path.

5. **Tool surface: read-only over the repositories + ONE confirmed write.** Tools (each a small
   class in `klr_agent`, `spec()` + `invoke(args, nowUtc)`, no SQL, tested on in-memory fakes):
   `list_plants`, `read_plant_data` (latest readings + range deltas, and a windowed min/avg/max
   history variant), `read_plant_journal`, `read_catalog_care_text` (the offline plant catalog — covers
   most of what a web tool would), and the single write tool **`add_journal_entry`** — an
   agent-authored journal entry, **marked as agent-authored and requiring user confirmation**
   before it executes (the confirm-before-write rule). "Save AI notes" **is**
   the journal entry for now; a separate per-plant (and global) agent-memory facility is now planned
   as the **per-plant + global memory milestones** — also a journal entry, but a dedicated `Memory` kind written through one
   replace tool (see decision 11), not this confirmed free-text note. Read-only tools + a confirmed write is also the
   prompt-injection posture: journal text (and any future web page) is untrusted model input, and
   the only write path is user-gated. `read_plant_photo` arrives with the vision milestone, gated on `ModelCaps.vision`
   (journal photos, cross-ref the planned journal-photo support). **Agent memory (per-plant +
   global) is now planned as the per-plant + global memory milestones** — see decision 11; the "scratchpad" is no longer
   parked.

6. **Reasoning is a consequence of decision 2 plus one knob.** A normalized request enum
   `ReasoningEffort { Off, Low, Medium, High }` mapped per dialect (OpenAI `reasoning.effort`,
   Anthropic/Gemini thinking budgets); `ReasoningDelta` streamed and shown collapsible in the chat
   UI; the opaque echo (decision 2) preserves fidelity within a turn's tool loop; the compat dialect
   extracts `<think>…</think>` from local models into `Reasoning` blocks. Persistence may drop
   reasoning across sessions — fidelity is only required within a turn.

7. **Chat-first UI.** The first surface is a plain chat screen behind the already-reserved
   `AIInsights` route (`NavigationController` enum): `AgentViewModel` (`QAbstractListModel` of turn
   rows + `Q_INVOKABLE sendMessage` + `streaming`/`pendingConfirmation` properties, injected per
   ADR 0002), a `ListView`, a `TextField`, zero `.js`. The insight-card feed is a **later
   layer over the same tools**, not the v1 shape. All AI affordances use the **Cyan accent**
   (`colorAI`) per the brand rule (ADR 0013 #5).

8. **Bounded agent loop, typed errors.** `AgentSession` owns one conversation: hard
   `maxIterations ≈ 8`, per-turn timeout, cancellation, `AgentError` surfaced (never a spin) —
   the deep-dive's non-negotiable guard, fully tested against a scripted `FakeProvider` (tool
   error, malformed args, loop limit, cancel mid-stream) before any real model is attached.

9. **Privacy: consent before anything leaves the device; keys in the Secret Service.** A pre-send
   disclosure shows *exactly* what a remote provider will receive (the rendered context block +
   any images). API keys/endpoints live in the freedesktop **Secret Service** (D-Bus, like the
   notifications path) — never `QSettings`. The web tool sits behind the same consent surface,
   fetches only from a curated 2–3-domain allowlist chosen by the *host* (the model never picks
   URLs), and reduces HTML to readable text before it enters context — it is the design's largest
   prompt-injection aperture and stays minimal by policy.

10. **Out of scope until mobile support lands:** `AiCoreProvider`, `AppleFoundationProvider`, `LiteRTProvider`, and
    any mobile model-download UX. The `Provider`/`ModelCaps` seam is where they will plug in.

11. **Agent memory is a journal entry kind, read on demand, written through one replace tool.**
    Memory — facts the agent should carry across conversations — is modeled as a special **journal
    entry**, *not* a new store. Reusing the journal buys three things for almost no code:
    **transparency** (the user reviews memory alongside their care log — the most honest surface,
    per decision 9), **edit/delete** (the journal repository + UI already have them), and
    **sync-readiness** (journal entries are change-logged, so memory rides the planned sync work — unlike the
    device-local transcript, which never syncs). A new pinned `JournalEntryKind::Memory` (appended,
    never renumbered, per the stable-wire-format rule); **exactly one** Memory entry per scope (one
    living blob), **rewritten in place** on update (a stable id — sync-friendly, no UUID churn).
    - **Per-plant memory** = a `Memory` entry on the plant. **Global memory** = a `Memory` entry on a
      new *global journal* — plant-less journal entries (optional scope) with their own UI surface.
    - The user **curates**: the journal UI renders Memory entries (cyan `colorAI` accent) and allows
      **edit/delete**, but **hides create** — memory is agent-authored, and the kind itself signals
      that (no `author` field needed, sidestepping the earlier no-author-field gap).
    - The agent **writes** through one narrow tool per scope — `set_plant_memory(plant_id, text)` /
      `set_global_memory(text)` — the repository fulfils it as an **in-place rewrite** of the single
      Memory entry's text (insert if none; one entry per scope is a repository invariant, no DB
      constraint needed), keeping the entry's **stable id**. The agent never sees a raw add/delete
      and never gets a general edit-or-delete-any-entry capability — decision 5's minimal write
      surface holds.
    - **Two timestamps order the journal honestly.** Every journal entry carries an **entry date**
      (set once at creation) and an **edit date** (bumped on every edit); the journal **sorts by
      entry date**. A *user* edit — even of a Memory entry — moves only the **edit date**, so
      curating a note never repositions it. An *agent* memory write bumps **both** dates, **floating
      the Memory entry to *now*** — so its place in the timeline is exactly when the model last
      revised it, and both the user and the model can read its chronological standing at a glance.
      **Schema:** add `ts_edited` alongside the existing entry timestamp — nullable, migrating
      existing rows to **NULL** (`nullopt` = *never edited*, not "edited at creation time"). Because
      this touches **all** journal entries (not just Memory) and is a general journal improvement
      rather than AI-specific, it landed as its **own standalone foundational step** —
      **ADR 0020** (`JournalEntry::editedAt` + schema v6) — which the per-plant memory milestone then builds on.
    - The agent **reads** memory **explicitly via a tool** — **never auto-injected as ambient
      context** (memory can grow large, the cache decision (below) keeps the always-on context minimal,
      and mutable text must never enter the cacheable stable prefix). The exact **read shape is left
      open**, to be settled by what works best for the agent in practice: a dedicated
      `read_plant_memory` / `read_global_memory`, or — for the per-plant case — folding it into the
      existing `read_plant_journal` (the Memory entry already surfaces there). Global memory, having
      no plant journal, needs a read path of its own regardless.
    - **Unconfirmed but visible.** Unlike `add_journal_entry`, a memory write executes without a
      confirmation gate — journal visibility + user edit/delete + the narrow set-only capability are
      the oversight (memory only feeds future advice, it never auto-executes). Global memory has the
      wider blast radius (it colours advice for *every* plant); a confirmation gate there is the
      recognized escalation if wanted.

## Roadmap (each milestone = its own granular-commit pass, ctest green throughout)

| Milestone | Contents | Exit criterion |
|-----------|----------|----------------|
| **Harness skeleton** ✅ | `karness` + `klr_agent` in the CMake graph; `Message`/`ContentBlock`/`ToolSpec`/`ModelCaps`/`StreamEvent`/`AgentError`; `Provider` interface; standalone-build CI gate | Types round-trip reasoning/tool blocks (unit tests, no network); `karness` builds alone |
| **SSE + compat dialect** ✅ | SSE parser; Chat Completions encode/decode incl. streamed tool calls; fixture suite + mock server | Ollama/llama-server/BYOK covered, fully offline-tested |
| **Agent loop** ✅ | `AgentSession` per decision 8; scripted `FakeProvider` edge tests | Every loop edge headlessly green |
| **Tools + context** ✅ | `ContextBuilder` (pure, clock-injected, `std::optional` — no sentinels); **four** tools (`list_plants`, `read_plant_data`, `read_plant_journal`, confirmed `add_journal_entry`) — `read_catalog_care_text` postponed, see below; in-memory-fake tests | Whole tool surface green; deterministic context block |
| **UI, persistence, consent** ✅ | `AgentViewModel` + chat screen on `AIInsights`; `AgentRepository` (conversation/message tables, migration); provider settings UI (BYOK endpoint/key); Secret Service storage; pre-send disclosure | **First end-to-end usable agent** (Ollama or any BYOK endpoint) |
| **Native dialects + reasoning** ✅ | Responses, Anthropic, Gemini dialects; `ReasoningEffort` knob; collapsible reasoning UI; fixture suites each | All four dialects conformance-tested; reasoning UX + native dialects done (see below) |
| **Vision + photo tool** ✅ | *(ADR 0025, on the journal photo storage of ADR 0024.)* `Image` plumbing per dialect (the four `rejectImages()` stubs replaced with real `image_url` / base64-`source` / `inlineData` / `input_image` encoding; the tool-result→user-block hoist for the text-only Chat Completions + Responses tool messages); `read_plant_photo` (read-only, returns a plant's journal photos as image content); an opt-in `agentVisionEnabled` setting (default off) gating `ModelCaps.vision` + tool registration + the prompt; transcript thumbnails + a pre-send disclosure showing thumbnails of every image leaving the device | Photos reach vision-capable models on every dialect; per-dialect conformance-tested offline |
| **Web tool** ✅ | *(ADR 0023.)* `read_online_plant_db` per decision 9: a host-curated allowlist (Wikipedia + Wikispecies; the model picks a *source by name*, never a URL), WatchFlower's species→slug mapping, an `IWebFetcher` seam (`NetworkWebFetcher` real / `FakeWebFetcher` test) so the first async tool tests offline, pure `htmlToText` reduction + truncation, and an **opt-in `agentWebToolEnabled` setting (default off)** gating both registration and the prompt/disclosure text | Allowlisted, consented, extracted-text only |
| **In-process llama.cpp** | **Spike first** (deep-dive go/no-go) → vendored provider, grammar-constrained tool turns, `ModelManager` (download/SHA/storage consent) | Zero-setup local; explicitly droppable on no-go |
| **Per-plant memory** ✅ | *(ADR 0021; builds on the standalone **dual-timestamp journal** step, ADR 0020.)* `JournalEntryKind::Memory = 6` (pinned, appended) + backup token + label; the unconfirmed `set_plant_memory` tool (in-place rewrite of the single Memory entry — same stable id, both dates bumped so it floats to *now*; create if none), registered un-decorated; the system prompt names it; the journal UI feeds the create-picker a `creatableJournalKinds` list that omits Memory, renders Memory with the cyan accent (`isMemory` role) and surfaces the edit date, keeps user edit/delete (a user edit of a Memory entry moves only the edit date and can't convert any other entry *into* Memory); the read path folds into the existing `read_plant_journal` — a dedicated `read_plant_memory` is **deferred** (decision 4: empirical, only if recall needs it); in-memory-fake tests | Agent records & later recalls a per-plant fact; the fact is visible + curatable in the plant's journal, positioned at its last agent revision |
| **Global memory + global journal** ✅ | *(ADR 0022; builds on the per-plant memory `Memory` kind + tool shape.)* `JournalEntry::plant` → `std::optional<PlantId>` + schema **v7** (nullable `plant_id`, table rebuilt) + `IJournalRepository::globalEntries()`; backup serializes/round-trips global entries; the unconfirmed `set_global_memory` (in-place rewrite of the single global Memory entry — same stable id, both dates floated; create if none) + the dedicated `read_global_memory` (no global plant-journal to fold into), both registered un-decorated with system-prompt guidance; a full-page **GlobalJournal** route reached from the AI assistant, reusing the per-plant journal delegate (cyan `Memory`, edit date, edit/delete) over `AppContext.globalJournal` with a `globalCreatableJournalKinds` list (Note only); in-memory-fake + migration tests | Agent records & recalls a user-wide fact; the global journal renders and is curatable |

The first five milestones (through *UI, persistence, consent*) are the critical path (that milestone = first user value); the rest are mutually independent and
reorderable, except *In-process llama.cpp* is explicitly last and spike-gated. *Global memory + global journal* depends on *Per-plant memory*
only for the shared `Memory` kind + tool shape.

**Prerequisite (ADR 0020): the dual-timestamp journal model.** Every journal entry
gains a `ts_edited` alongside its entry date; the journal **sorts by entry date** and a *user* edit
moves only `ts_edited`. This is a general journal/persistence improvement, **not** AI-specific, so it
ships as its own ADR + schema-v6 migration ahead of the per-plant memory milestone — the per-plant memory float (an *agent* write bumps
**both** dates) then builds on it. *(The `Memory` kind value is **6**, the next free
`JournalEntryKind` in klorophylle — the enum runs `Note=0`…`Observation=5`.)*

**Harness skeleton.** `karness` lives at `src/karness/` (namespace `karness`); its dual-mode
`CMakeLists.txt` builds as a klorophylle child or as a top-level project, and CI's "karness
standalone build" step is the extraction gate. The provider interface follows the house `IFoo`
convention (`IProvider`, no QObject) with the streaming contract documented in `iprovider.h`
(promise `start` → one `addResult` per event → exactly one terminal `Done` XOR `ErrorEvent` →
`finish`; never `setException`; `cancel()` aborts the transport). Beyond the spec'd types, this milestone also
includes `MessageAccumulator` (pure delta→`Message` assembly all dialects reuse; parallel tool calls
keyed by stream index; empty args → `{}`, malformed → `Code::Parse`) and pins `QFuture`'s
default+copy-constructible requirements on `StreamEvent` with `static_assert`s (the variant's first
alternative is load-bearing). `Done.stopReason` is a normalized enum (`EndTurn/ToolCalls/MaxTokens/
ContentFilter/Other`) and `Done` carries optional `TokenUsage`; `InferenceRequest.temperature` is
optional (omitted from the wire when unset — some providers reject it on reasoning models). The
codec is strict (`messageToJson`/`messageFromJson` → `std::expected<Message, MessageCodecError>`,
abort-on-error base64). `klr_agent` is a one-TU seam pinning `karness + klr_core + klr_persistence
→ klr_agent` until the tools-and-context milestone.

**SSE + compat dialect.** Four pieces, all in `karness`: `SseParser` (pure WHATWG
`text/event-stream` framing — the `[DONE]` sentinel is **dialect-level**, the parser stays
provider-agnostic since Anthropic terminates via `event:` types); the pure
`karness::chatcompletions` codec (`encodeRequest`/`decodeChunk`/`mapFinishReason`); a
`ThinkTagSplitter` (decision 6's `<think>` extraction lands *here*, not in the native-dialects milestone: a streaming splitter,
leading-tag-only so prose mentioning the tag is never mangled, tag-padding whitespace dropped,
unterminated think streams as reasoning; the structured `reasoning_content`/`reasoning` delta
fields decode in the codec, so both reasoning transports emit identical `ReasoningDelta`s); and
`OpenAiCompatProvider` (config: `baseUrl` + optional `apiKey` + host-declared `caps` + stall
timeout). Notable choices: plain `QNetworkAccessManager`, not a `QRestAccessManager` (streaming
reads `QNetworkReply::readyRead` either way; QRest adds nothing); `temperature`/`seed`/`max_tokens`
omitted when unset and `tools` omitted when empty (some compat servers 400 on `[]`);
`reasoning_effort` passthrough included (trivial; the native-dialects milestone still owns cross-dialect mapping); `ImageBlock`
on encode is a **hard error** until the vision milestone (never silently dropped), `ReasoningBlock` on encode is
dropped (no wire slot, no signatures to preserve); one turn in flight per provider instance; the
transport carries only a *stall* guard (`setTransferTimeout`, resets per byte; surfaces as
`TimeoutError` here though documented as `OperationCanceledError` — both accepted, told apart from
`cancel()` by a flag) while the wall-clock turn budget waits for the agent-loop milestone. Error taxonomy: non-2xx →
`Http` (OpenAI-object and Ollama-string bodies parsed), TCP drop → `Network`, undecodable chunk /
clean end without completion / malformed accumulated tool args → `Parse`; a clean end after a
`finish_reason` but without `[DONE]` is leniently a `Done` (proxies drop the sentinel). Conformance
fixtures are **hand-authored from documented wire formats** (no live endpoint is used; provenance
noted in `tests/karnessfixtures.h` — replace with live captures when at hand) and replay through a
chunked-encoding `MockHttpServer` at hostile chunk offsets; chunked is load-bearing (clean 0-chunk
end vs. distinguishable mid-stream drop). Test-layer note: fixture payloads avoid raw-string edge
cases (embedded `\"` or unbalanced braces) via a `'`/`` ` `` substitution helper.

**Agent loop.** `AgentSession` lives in `karness` (decision 1: the harness owns the
bounded loop) as a QObject with signals — the UI milestone's viewmodel consumes live `textDelta`/`reasoningDelta`/
`toolCallStarted`/`toolCallFinished` directly; synchronous preconditions return `std::expected`
(busy → `Provider`, `!isReady()` → `NotReady`); exactly one terminal `turnFinished(StopReason)` XOR
`turnFailed(AgentError)` per accepted send, emitted strictly last. The tool seam is `ITool`
(`spec()` + async `invoke(args) → QFuture<ToolOutcome>`; the future must *finish* on every path,
failures as `isError` values) — async so the UI milestone's user confirmation is just a decorator tool whose
future waits for the user, with zero confirmation knowledge in the loop; the tools-and-context milestone binds its injected
clock inside the adapter, karness stays time-free. Outcomes are delivered through one
`QFutureWatcher` per call (event-loop delivery even for ready futures) and gathered slot-indexed,
so the single `Role::Tool` message keeps original call order whatever the resolution order; an
abandoned turn (timeout/cancel) is a stale generation — tool futures are never cancelled from the
loop side. Loop policy: tools run on the *presence* of `ToolCallBlock`s under `EndTurn`/`ToolCalls`
(compat servers say `stop` alongside calls), `ToolCalls`-without-calls is a loud `Parse`,
`MaxTokens`/`ContentFilter`/`Other` finish successfully with the partial message kept and flagged
via the signal's reason (tools never run — args may be truncated); unknown tool names and
tool-reported errors re-inject as `isError` results (standard recovery practice) and count toward
`maxIterations` (default 8 provider calls) → `LoopLimit`. The per-turn budget is a whole-turn
single-shot `QTimer` (default 5 min, 0 disables; covers tool time — the UI milestone's confirmation must budget
for it); both timeout and `cancel()` are **fail-first** (terminal emitted, generation bumped,
watcher detached, *then* `provider.cancel()`), which is what keeps the transport's late `Cancelled`
from masquerading as the terminal. History invariant: only complete successful turns — failures
truncate to the pre-send baseline (a dangling `ToolCall` without its result is invalid wire history
on every dialect); the system prompt is settable per send and never enters `history()`; `Done`'s
assembled message is appended **verbatim** (no second accumulator — preserves
`ReasoningBlock::providerOpaque` through the loop). `FakeProvider`/`FakeTool` live in `tests/` (the
standalone gate stays minimal): async one-event-per-loop-pass pumping, full streaming contract
including cancel/destruction terminals, loud failures on script-authoring errors; the edge matrix
(37 slots in `test_agentsession`, including the doubles' own contract smoke) runs offline in ~0.5 s.

**Tools + context.** All in `klr_agent` (`karness + klr_core + klr_persistence`, never
`klr_gui`), tested headlessly on the in-memory fakes + `FakeClock`. `ContextBuilder` is **roster
only**: the plant list + a count, stably sorted oldest-first so `build()` is
byte-stable for a given repository + clock (the determinism exit criterion) — readings/care status
are left to the tools, keeping the always-included prompt small and the data-exposure surface
minimal. The tools are plain `karness::ITool`s in one `agenttools.{h,cpp}` TU sharing a `resolvePlant`
helper (missing / blank / malformed / unknown `plant_id` all map to `isError` outcomes the model
recovers from, never turn failures) and ready-future / object-schema helpers; all four return ready
futures (the repositories answer in-process — the async `ITool` contract still holds, so the UI milestone's
confirmation decorator wraps `add_journal_entry` without these tools knowing). `read_plant_data`
reuses the **pure klr_core** `evaluate`/`rangeFor`/`extremesOf` helpers for the ideal/below/above
verdict, *not* the `klr_gui` `evaluatePlantCare` orchestrator (which sits above this layer); journal
kind labels/parsing reuse `klr_persistence` `backuptokens::toToken`/`fromToken`. `add_journal_entry`
ships as the bare write (decision 5) — confirmation is the UI milestone's decorator, and the "agent-authored"
marking waits for the UI milestone's `AgentRepository` migration (`JournalEntry` has no author field yet).

**UI, persistence, consent.** Schema **v5** adds `agent_conversations` + `agent_messages`
(device-local like `sensor_sync_state` — NOT change-logged, so a transcript never syncs);
`klr_persistence` stays karness-free by storing each message as a `role` int + an opaque
`content_json` blob, and `klr_agent`'s `transcript::load/appendAll` map `karness::Message` ⇄ those
rows via the canonical `messageToJson/messageFromJson` codec (one bad row is skipped, never fatal).
The user-confirmation gate is `ConfirmingTool` (a `QObject` `ITool` wrapping `add_journal_entry`:
`invoke()` returns a `QPromise` future left pending until `approve()`/`reject()`, zero loop
knowledge — decision 8). `AgentViewModel` (in `klr_gui`, now linking `klr_agent`) is a
`QAbstractListModel` that drives `AgentSession` over the four tools + the decorator, **builds its
provider behind an injected factory** (production → `OpenAiCompatProvider`, tests → `FakeProvider`)
rebuilt on a config change, renders committed rows authoritatively from `history()` on
`turnFinished` (live partial text via a `streamingText` property — so non-streamed terminal text
still shows), and persists each finished turn's new messages. The chat screen is the `AIInsights`
route (a full-width page off the nav-rail "More" menu) using `PulsingNode` for the cyan AI accent;
**reasoning UI comes with the *Native dialects + reasoning* milestone**. Non-secret config (enable/endpoint/model) lives in `SettingsStore`
defaulting to a local Ollama; the API key goes only through the `ISecretStore` seam —
`FreedesktopSecretStore` (org.freedesktop.secrets over D-Bus, exe-only like the notification sink,
best-effort/fail-soft) with an `InMemorySecretStore` fallback when no session bus/keyring is
present (a local endpoint needs no key). Pre-send disclosure fires on the first message to a
**non-localhost** endpoint each session, rendering exactly what will leave the device (system
prompt + context roster + the user message); localhost skips it. Exit criterion met: a local
Ollama lists plants, reads data, and proposes a journal entry the user confirms.

**UI follow-up — usability polish.** Three small fixes from exercising the agent
against a real local model: (1) **discoverability** — the AI assistant is a **top-level rail
destination** beside Plants and Sensors (out of the NavRail "More" overflow; still the full-width
`AIInsights` page; `NavigationController.push`, active on `currentRoute`). (2) **Scrolling** — the
chat transcript's passive `ScrollIndicator` is an interactive `ScrollBar`, and the Settings
screen (previously a non-scrolling `ColumnLayout` that clipped overflow) is wrapped in a
`ScrollView` with a vertical bar. (3) **Small-model escape hatch** — a small local model can be
overwhelmed by four tool definitions, so a persisted **`agentToolsEnabled`** preference on
`SettingsStore` (default on, emits the existing `agentChanged` → session rebuild) now gates the
`AgentSession::setTools(...)` call from a Settings → AI assistant toggle, and `buildSystemPrompt()`
branches on it (tools-on instructs "use the tools / call `add_journal_entry`"; tools-off tells the
model it has none and to answer from the roster). Default-on keeps the UI-milestone confirmation tests green.

**`read_catalog_care_text` is postponed (it would have been a fifth read tool).** Decision 5 assumed the offline plant
catalog carried species *care text*. It does not: the bundled `plantdb.csv` care columns
(Sunlight/Watering/Fertilizing/Pruning/Soil, cols 16–20) hold **coded values** (`sun=3-4`, `wat=2,6`),
not prose, and the catalog parser deliberately ignores them — `CatalogEntry` carries only the numeric ideal
ranges. The human-readable text exists only as decode tables in WatchFlower's QML JS
(`qml/components_js/UtilsPlantDatabase.js`: `getWateringText`/`getSunlightText`/…). Surfacing real care
guidance therefore needs those tables ported to C++ first (a `klr_core`/`klr_persistence` lift), which
is its own piece of work; until then the tool would only restate the numeric ranges `read_plant_data`
already gives. Build it (against ported care text) before/with the native-dialects milestone, or fold it into the catalog when
care text is added.

**Native dialects + reasoning — reasoning UX first.** This milestone splits into its two independent halves, with the
**reasoning UX** half built first — it delivers visible value on the *existing* compat dialect with
**no new provider code** (the wire path already mapped `reasoningEffort` → `reasoning_effort`, reasoning
already streamed as `ReasoningDelta` from `reasoning_content`/`reasoning` and `<think>` via
`ThinkTagSplitter`, and `AgentSession::reasoningDelta` already fired). What it adds: a persisted
**`agentReasoningEffort`** preference on `SettingsStore` (an int matching `karness::ReasoningEffort`
order — Off/Low/Medium/High — clamped, device-local, driving the existing `agentChanged` session
rebuild; surfaced as a Settings → AI assistant ComboBox); `AgentViewModel` now sets
`tmpl.reasoningEffort` on the request template (dialect-neutral, so the native dialects map it
per-provider later) and `caps.reasoning = true`, **connects `reasoningDelta`** into a new
`streamingReasoning` property, and renders a new **`ReasoningKind`** row (it previously *dropped*
`ReasoningBlock` in `appendRowsFor`). The UI is a reusable **`ReasoningDisclosure`** (a collapsed-by-
default cyan disclosure in `Klorophylle.Style`) used both for the live streamed reasoning (chat footer)
and committed reasoning rows. **No schema change** — reasoning already round-trips through the opaque
`content_json` blob via `messageToJson`, so a reloaded transcript re-renders the disclosure. Covered by
`test_agentviewmodel` (reasoning streams → renders → persists → reloads; effort reaches the provider's
`InferenceRequest`) and `test_settingsstore` (default/persist/clamp). **Still in this milestone: native dialects** —
the OpenAI Responses / Anthropic Messages / Gemini `generateContent` dialects behind an explicit
**"Provider type" selector** + factory branching, each a new codec/provider with a fixture suite,
mapping `ReasoningEffort` per-provider and populating `ReasoningBlock::providerOpaque`.

**Native dialects.** The compat-dialect transport is ~95% dialect-agnostic, so it is
lifted into a reusable **`StreamingProvider`** (QObject: sockets, stall-timeout guard, the
HTTP/network/cancel/timeout error taxonomy, the single-terminal + destruction guarantees) driven by
a pure, **stateless `Dialect`** strategy (`endpoint` / `applyAuth` / `encodeRequest` / `decodeEvent`
/ `isTerminalSentinel` / `extractsThinkTags`); `DecodedChunk` + `ProviderConfig` move to shared
headers (`OpenAiCompatConfig` kept as an alias). `OpenAiCompatProvider` is a thin
`StreamingProvider` + `ChatCompletionsDialect`, its unchanged conformance suite guarding the
refactor. The three native providers are each `StreamingProvider` + one dialect:
**`ChatCompletionsDialect`** (compat, `[DONE]`, `<think>` extraction), **`AnthropicDialect`**
(`/messages`, `x-api-key`+`anthropic-version`, system hoisted, tool results on the user turn,
`thinking.budget_tokens`, completes on `message_stop`), **`ResponsesDialect`** (`/responses`,
typed `input` items, flat tools, `reasoning.effort` + stateless `store:false` /
`include:[reasoning.encrypted_content]`, completes on the stream end after `response.completed`),
**`GeminiDialect`** (`:streamGenerateContent?alt=sse`, `x-goog-api-key`, `systemInstruction`,
`functionDeclarations`, `thinkingConfig.thinkingBudget`, bare SSE chunks, synthesized call ids).
Reasoning opaque (Anthropic `signature`, Responses reasoning-item id + `encrypted_content`, Gemini
`thoughtSignature`) rides a new optional `ReasoningDelta::providerOpaque` that `MessageAccumulator`
merges onto the reasoning block, so it round-trips a tool loop; `StreamingProvider` now **merges**
usage frames (Anthropic splits input/output across `message_start`/`message_delta`). The shared
`ReasoningEffort`→budget map lives in `reasoningbudget.h`. Selection is a persisted, clamped
**`agentProviderType`** on `SettingsStore` (0=compat, 1=Responses, 2=Anthropic, 3=Gemini) → a
**`ProviderFactory(int, ProviderConfig)`** branch in `AgentViewModel` → a "Provider type" ComboBox
in the Settings AI section. Each dialect has a `MockHttpServer` conformance suite
(`test_anthropicprovider` / `test_responsesprovider` / `test_geminiprovider`: text, tool calls,
reasoning + opaque into the `Done`, request wire shape, provider/HTTP errors, drop/cancel/timeout) at
hostile chunk offsets; fixtures hand-authored from each documented wire format (provenance noted in
`tests/karnessfixtures.h` — replace with live captures when an endpoint is at hand). `karness` still
builds standalone (the extraction gate). **Vision/images remain a hard encode error on every dialect
until the vision milestone.**

## Not decided here (follow-ups)

- Conversation/message **schema** detail (block serialization, retention) — with the UI-milestone migration.
- `read_catalog_care_text` + the WatchFlower care-code → text port it needs — before/with the native-dialects milestone (see the tools-and-context note).
- The **insight-card** layer and any proactive analysis — after the native-dialects milestone, as a layer over the same tools.
- ~~Per-plant **agent memory** (the unconfirmed scratchpad) — recognized extension of decision 5.~~
  **Promoted to the per-plant + global memory milestones** (decision 11): per-plant + global, modeled as a `Memory`
  journal-entry kind, read on demand, written through one transactional replace tool.
- **User-editable entry date (backdating).** Decision 11's dual-timestamp model sets the **entry
  date = creation time, immutable for user entries** (deliberately simple for now — the timeline is
  "when I logged it"). A care log often wants the *event* date to differ from the log date ("I
  repotted *last Tuesday*", logged today), which today would sort at the log time. **Planned
  follow-up:** let the user pick/edit a journal entry's entry (event) date — either by making the
  existing entry date editable or by adding a distinct settable event date that drives the sort
  while created/edited stay machine-set. Out of the per-plant + global memory scope; its own small UI + (if a
  separate field) schema step. Note the agent memory float (decision 11) still keys on the entry
  date, so a backdated *Memory* entry would reposition — keep memory agent-set.
- **Conversation management UI** — the UI milestone persists every finished turn to `agent_conversations`/
  `agent_messages` (device-local, never sync'd) and the chat resumes the newest one on launch +
  offers "New conversation", but there is **no UI to browse, title, or delete** past conversations,
  so they accumulate in the DB unbounded. A conversation list/picker + per-conversation delete
  (the repository already has `createConversation`/`conversations()` and a cascade-deleting
  `deleteConversation`) — pairs naturally with the retention question above.
- **Resumed-conversation memory** — resuming a conversation today is **display-only**: prior turns
  are rendered into the view but the session's `history()` starts empty, so the model does not
  remember anything across an app restart (only the system prompt + context roster + the new
  message). Feeding the loaded transcript back into `AgentSession` history on resume (bounded by
  `ModelCaps.ctxTokens`, so older turns are dropped/summarized) would make resume actually
  continue the conversation — its own piece of work, mind the context-window budget.
- **Cache-friendly context placement (prefix vs tail).** The UI milestone concatenated the deterministic
  context block (`ContextBuilder::build()`) *into* the system prompt (`buildSystemPrompt()`),
  i.e. into the **cacheable prefix**, and re-sets it every send. Prompt caching (Anthropic/OpenAI)
  and local llama.cpp/Ollama KV-cache reuse all key on a **stable leading prefix**, so any change
  to the context invalidates the whole prefix (instructions **+ tool schemas + …**) and forces a
  full re-process — a direct TTFT/latency hit for local models. Today it mostly holds because the
  context is roster-only and byte-stable within a sitting, but it breaks on (a) any roster edit
  mid-session and (b) the `m_clock.nowMs()`-derived "tracked N days" rolling over **midnight** — a
  clock value in a cached prefix is the anti-pattern. **Decision:** keep the system prompt a
  **stable prefix** (instructions + tool schemas only, no clock-derived values); deliver per-turn
  context at the **tail** (attached to / just before the latest user message). Live data already
  comes from tools, so the always-on context stays minimal. *Division of responsibility:* the
  stable-prefix contract is **karness's** (it already keeps the system prompt out of `history()`),
  and the protocol-level **prompt-cache primitives** — per-dialect `cache_control`/cached-content
  breakpoints (Anthropic ephemeral, Gemini cached content; OpenAI auto) — belong in karness's pure
  request encoders, landing naturally with the native dialects. But *deciding what is
  volatile* (a plant roster) stays with **klr_agent** — karness can't know.
  **The system prompt becomes a constructor invariant; `setSystemPrompt` is removed.** A session's
  behavioural contract is fixed at birth — mutating it mid-conversation is incoherent (the existing
  `history()` was produced under a different constitution) and is exactly what lets volatile text
  leak into the prefix. So the system prompt is passed to the `AgentSession` constructor and is a
  **lifetime invariant**; changing it means **constructing a new session**, not calling a setter.
  This *mechanically* guarantees the stable cacheable prefix (there is no setter to drift it), and
  it already matches `AgentViewModel`, which rebuilds the whole session on any config change
  (`m_sessionDirty` → `session.reset()` + recreate) — "new prompt ⇒ new agent" is the existing
  lifecycle. `setSystemPrompt` only ever existed to refresh context per turn, which was the bug.
  **karness gains an ambient-context seam instead.** The one piece that legitimately varies per
  turn — implicit/ambient context — gets an explicit seam injected at the **tail** and **never
  entering `history()`**: `AgentSession::setAmbient(QString)` (set once, re-read every turn,
  refreshed by the caller when it changes) and/or a per-send `send(ambient, message)`. `setAmbient`
  is the leaning shape (the caller controls when it changes); the request is assembled as
  `[System(invariant), …history, ambient+user]`, so the system+tools+prior-history prefix stays
  cacheable and only the final turn carries the fresh ambient. This makes the cache-safe path the
  *only* path. **klr_agent** then splits today's `buildSystemPrompt()`: the stable instructions
  (+ tool guidance) become the session's constructor prompt, and `ContextBuilder::build()` (sans
  clock-derived values) flows through `setAmbient`. Near-term: the `AgentSession` API change
  (prompt → ctor, drop the setter, add ambient) + the `AgentViewModel` split; the cache-control
  breakpoint support is the native-dialects karness piece.
  **API cleanup.** `AgentSession`'s system prompt is now a
  constructor argument backed by a `const QString` member (no setter to drift it — `setSystemPrompt`
  removed), and `setAmbient(QString)` injects per-turn context at the tail — `buildRequest()`
  prepends it as a leading `TextBlock` on the latest `Role::User` message of the *outgoing copy*
  only, so `m_history` stays bare and the system+tools+prior-history prefix is byte-stable across
  turns (covered by `test_agentsession::ambientPrependedToLatestUserNeverStored`). `ContextBuilder`
  drops its `Clock` dependency entirely — `build()` is now roster-only (`name (species)`, no
  "tracked N days"), so no clock value can reach a cached prefix. `AgentViewModel` splits
  `buildSystemPrompt()` into `buildInstructions()` (the stable ctor prompt, chosen by
  `agentToolsEnabled`) and a per-send `setAmbient(m_context.build())`; the pre-send disclosure still
  shows instructions + context. The leaning `setAmbient` shape is taken (no per-send
  `send(ambient, message)` overload).
  **Follow-up cleanup.** Three deferred items land. (1) `setRequestTemplate(Inference
  Request)` (which silently overwrote `messages`/`tools`) becomes `setModelConfig(ModelConfig)`,
  a knobs-only struct (`model` + `reasoningEffort` + `temperature`/`seed`/`maxTokens`); `InferenceRequest`
  is unchanged (still the provider/codec input). (2) **`cache_control` breakpoints** via a new
  `InferenceRequest::cacheStablePrefix` flag the session sets (the ctor-invariant prompt + fixed tools
  *are* the stable prefix): the **Anthropic** codec emits the system block as a content-block array
  carrying `cache_control:{type:ephemeral}` (render order tools→system→messages, so one breakpoint caches
  both; falls back to the last tool when there is no system block); the OpenAI Chat Completions/Responses
  codecs ignore it (caching is automatic) and Gemini ignores it (implicit caching; explicit `CachedContent`
  is a separate resource, out of scope) — all documented inline. (3) The user's **unit preferences** now
  ride in the stable prompt (`buildInstructions()` + `buildUnitPreferences()` reusing klr_core
  `displayUnit`/`unitSymbol`), with `unitsChanged → m_sessionDirty` so a unit change rebuilds the session.
  **Still open:** the optional rendering of `Role::System` as an OpenAI `developer` message — only
  beneficial against real OpenAI (not the shared compat/Ollama path), and it needs provider identity in
  the codec seam (the codecs are pure `encodeRequest(const InferenceRequest&)` today), so it's a localized
  later refinement, not a model change.
- `QtTaskTree` for the loop remains rejected while Technical Preview; plain
  `QFuture`/`QPromise`.
