# ADR 0023 — Web tool: `read_online_plant_db`

**Status:** accepted · **Builds on:** ADR 0019 (the agent domain tools / `agenttools`, `AgentViewModel` + pre-send
disclosure, the `karness::ITool` async contract), ADR 0002 (composition-root injection), ADR 0013
(cyan `colorAI` AI accent).

The agent reads the user's plants, sensor data and journal, and can write confirmed/memory journal
entries — but it has **no way to consult external plant knowledge**. This milestone adds **one narrow web-fetch
tool**, "a reputable-plant-encyclopedia reader at most" (ADR 0019 decision 9), so the model can pull
species background/care text into a conversation.

Decision 9 is the binding spec, and this ADR holds to it: the tool **sits behind a consent surface**,
**fetches only from a host-curated allowlist (the model never picks URLs)**, and **reduces HTML to
readable text before it enters context**. It is "the design's largest prompt-injection aperture and
stays minimal by policy" — so every decision below narrows the aperture rather than widening it.

## Decisions

1. **A host-curated two-domain allowlist: Wikipedia + Wikispecies.** The built-in allowlist is
   `en.wikipedia.org` and `species.wikimedia.org` — reputable, stable URL schemes, broad plant
   coverage. It is **compile-time, chosen by the host** (not the user, not the model) per decision 9.
   The model selects a *named source* (`"wikipedia"` | `"wikispecies"`), never a URL; the host maps
   the name → URL. A built-in list (not a user-editable setting) keeps the surface minimal and
   removes a user-supplied-domain injection vector.

2. **Species → page is computed, as WatchFlower did.** WatchFlower built its
   "Learn more" links from the botanical name with spaces → underscores
   (`Plant::name_botanical_url`, `src/Plant.cpp`; used in `qml/PlantScreen.qml` for its Wikipedia/RHS
   links) — **no DB URL column**. klorophylle's `Plant.species` already holds that botanical/catalog
   key (e.g. `"Aloe vera"`), and the agent already sees it in the roster + `list_plants`. So the tool
   takes a **`query`** string (the species name the model already has) and applies the same slug rule
   (trim, spaces → underscores, percent-encode the path segment) onto the chosen source's
   `/wiki/{slug}` template. Pages do **not** map 1:1 to the catalog, so a miss returns whatever the
   page yields (or a "no article" notice) and the model recovers — the same best-effort posture as
   legacy, no curated mapping table to maintain.

3. **HTML is reduced to plain text before it enters context.** A pure `htmlToText()` drops
   `<script>`/`<style>` blocks, strips tags, decodes common HTML entities, and collapses whitespace;
   the result is **truncated to a fixed budget (~8000 chars)** and prefixed with a one-line
   provenance note (`From en.wikipedia.org: …`). Raw HTML never reaches the model — narrowing the
   injection aperture and protecting the context window. Reduction + URL building + the allowlist
   check are **pure functions**, unit-tested with no network.

4. **The tool lives in `klr_agent`; `karness` is untouched.** All domain tools live in `klr_agent`,
   which already links `karness` (= `Qt6::Core + Qt6::Network`), so HTTP I/O belongs there. `karness`
   stays plant-agnostic and standalone-buildable — the CI extraction gate is unaffected (this milestone adds
   nothing to `karness`). `ReadOnlinePlantDbTool` is a plain `karness::ITool` (not a `QObject`): it
   is the **first genuinely async tool**, returning `m_fetcher.fetch(url).then([]{ … })` mapped to a
   `ToolOutcome`. The `ITool` contract already supports pending futures (`ConfirmingTool` proves the
   path). Argument/precondition failures (blank `query`, unknown `source`, non-allowlisted host) and
   transport failures (HTTP error, empty body) all return `isError` outcomes the model recovers from,
   never turn failures (ADR 0019 decision 8).

5. **A fetcher seam keeps tests offline (`IWebFetcher`), mirroring `ISecretStore`.** `IWebFetcher`
   returns `QFuture<WebFetchResult>` (`{ std::optional<QByteArray> body; int httpStatus;
   std::optional<QString> error; QUrl finalUrl; }`). `NetworkWebFetcher` (real, `QObject` +
   `QNetworkAccessManager`, in `klr_agent`) does a GET with `setTransferTimeout`, a response
   **size cap** (abort past ~2 MB), redirect policy `NoLessSafeRedirectPolicy`, and
   **re-validates the final URL host against the allowlist after redirects** (a redirect must not
   escape the allowlist). It is constructed in the composition root (`src/app/main.cpp`) and injected
   into `AgentViewModel` (ADR 0002 — no `getInstance`/`setContextProperty`). `FakeWebFetcher` (a
   header-only test double, like `InMemorySecretStore`) returns scripted results so the tool, the
   view-model and the loop stay headlessly testable.

6. **Consent is an opt-in setting, default OFF; the tool is registered only when on.** Even with a
   local LLM, fetching a page is network egress to a third party, so web access is **off by default**.
   `SettingsStore` gains `agentWebToolEnabled` (key `"agent/webToolEnabled"`, default `false`,
   `agentChanged()` signal). `AgentViewModel` registers `read_online_plant_db` only when
   `agentToolsEnabled() && agentWebToolEnabled()`, and `buildInstructions()` names it + its sources
   **only when web is on**. The Settings toggle ("Look up plants online") carries an explanatory
   caption naming Wikipedia/Wikispecies as the egress — **that informed opt-in is the consent**. The
   pre-send disclosure (the remote-LLM gate, ADR 0019 decision 9) gains a line, when web is on,
   noting the assistant may fetch from the allowlisted site(s). **Unconfirmed per call**: a web page
   is untrusted *input* (mitigated by text-only + allowlist + the opt-in gate), not a *write* needing
   per-call approval (decision 5/9) — matching the memory-tool posture.

## Implementation order (one commit each, `ctest` green at each)

1. This ADR (docs-only).
2. Pure helpers — `webcontent.{h,cpp}`: `htmlToText`, `speciesToSlug`, `sourceUrl`, `isAllowedHost`
   (+ `test_webcontent`: tag/script/style stripping, entity decode, whitespace collapse, truncation;
   the slug rule; allowlist accept/reject; unknown source → `nullopt`).
3. Fetcher seam — `iwebfetcher.h` + `WebFetchResult`; `fakewebfetcher.h`; `networkwebfetcher.{h,cpp}`
   (GET, timeout, size cap, redirect re-validation); wire into `src/agent/CMakeLists.txt`.
4. `ReadOnlinePlantDbTool` in `agenttools.{h,cpp}` (uses `IWebFetcher&` + the helpers)
   (+ `test_agenttools`: success → reduced + truncated text with provenance; blank `query` →
   `isError`; unknown `source` / non-allowlisted host → `isError`; HTTP error / empty body →
   `isError`; `source` defaults to wikipedia).
5. `SettingsStore::agentWebToolEnabled` (default false) (+ `test_settingsstore`: default/persist).
6. `AgentViewModel` — `IWebFetcher&` ctor param, `m_webTool`, conditional registration,
   `buildInstructions()`/`buildDisclosure()` web text (+ `test_agentviewmodel`: registered only when
   both toggles on; prompt names it; disclosure mentions it).
7. `main.cpp` — construct + inject `NetworkWebFetcher`. `SettingsScreen.qml` — toggle + caption
   (qmllint/cachegen).
8. Set this ADR's status accepted.

## Tests

`test_webcontent` (pure: `htmlToText` strips script/style/tags, decodes entities, collapses
whitespace, truncates to budget; `speciesToSlug` legacy rule; `sourceUrl` builds the right per-source
URL and percent-encodes; `isAllowedHost` accept/reject; unknown source → `nullopt`), `test_agenttools`
(`read_online_plant_db` over a `FakeWebFetcher`: success path returns reduced text + provenance;
blank `query`, unknown `source`, non-allowlisted host, HTTP error and empty body all → `isError`;
`source` omitted defaults to wikipedia), `test_settingsstore` (`agentWebToolEnabled` default false +
persists), `test_agentviewmodel` (web tool registered iff both toggles on; `buildInstructions()`
names it only when on; `buildDisclosure()` mentions web egress only when on). The Settings QML toggle
stays qmllint/cachegen-only by design.

## Out of scope / follow-ups

- **A user-editable allowlist** — deliberately not added (decision 1): a built-in host-curated list
  keeps the surface minimal and removes a user-supplied-domain injection vector. Revisit only if a
  real need appears.
- **A confirmation gate on web fetches** — deliberately not added (decision 6); the opt-in toggle +
  allowlist + text-only reduction are the posture. A per-call gate is the recognized escalation if
  the aperture proves a problem in practice.
- **Smarter main-content extraction** — `htmlToText` is a generic reducer, so a full page carries
  some nav/footer noise the model tolerates. A main-content heuristic (or a cleaner per-source
  endpoint) is a possible later refinement, not this milestone's minimal scope.
- **More sources / RHS-style search URLs** — WatchFlower also linked RHS via a search-query URL.
  The allowlist is two encyclopedias for now; broadening it (or adding a search-result reader) is a
  later allowlist decision, deliberately out of this milestone.
