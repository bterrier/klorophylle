# ADR 0027 — AI provider onboarding

**Status:** accepted · **Builds on:** ADR 0019 (the four provider dialects + the
`SettingsStore::agentProviderType` selector, the `ISecretStore` key seam), ADR 0023 / ADR 0025 (the
web + vision capability toggles), ADR 0008 (settings, the per-category Settings pages this surfaces
on), ADR 0002 (composition-root injection).

Connecting an AI provider is a wall of raw fields — a provider-type combo, a free-text endpoint, a
free-text model, an API key — all shown at once regardless of provider, with no guidance on *where
to get a key* or *which model to pick*. Worse, the endpoint field defaults to the local Ollama URL
(`http://localhost:11434/v1`) for **every** provider, so selecting Gemini, Anthropic or OpenAI
silently inherits the wrong base URL and a broken config unless the user already knows the right
host string.

This milestone makes connecting a provider easy for a non-technical user — especially the "I have a
Google account, give me free AI" path — **without caging the power user**, who must still be free to
point an OpenAI-compatible client at any endpoint.

## Decisions

1. **A single tested per-provider descriptor is the source of truth.** A pure C++ table keyed by the
   `agentProviderType` index (0 = OpenAI-compatible, 1 = OpenAI Responses, 2 = Anthropic, 3 = Gemini)
   describes each provider; every later piece — adaptive field visibility, model autocomplete, the
   key link, presets, the wizard — reads from it. So the screen carries no per-provider logic, and a
   wrong endpoint or key URL is caught by a unit test rather than shipped.

   ```
   ProviderDescriptor { displayName, fixedEndpoint, needsKey, keyUrl,
                        defaultModel, knownModels[], textOnlyModels[], freeTierUrl }
   ```

   It lives in `klr_gui`, next to its only consumers (the AI settings screen and `AgentViewModel`);
   it is presentation/onboarding metadata, not domain or wire data. `providerDescriptor(int)` clamps
   an out-of-range index to the default branch, mirroring the provider factory's `default:`.

2. **A fixed endpoint replaces the free-text field for the cloud providers.** Anthropic, OpenAI
   Responses and Gemini each talk to one well-known host; their dialects append a fixed path suffix
   (`/messages`, `/responses`, `/models/{model}:streamGenerateContent`). The descriptor carries the
   base each needs (`https://api.anthropic.com/v1`, `https://api.openai.com/v1`,
   `https://generativelanguage.googleapis.com/v1beta`); the screen **hides the endpoint field** for
   them. Only the OpenAI-compatible branch — the BYO-endpoint escape hatch covering Ollama,
   llama.cpp, vLLM, OpenRouter and a remote OpenAI — has an empty `fixedEndpoint` and keeps the field
   (defaulting to the local Ollama URL). `AgentViewModel` uses the descriptor's `fixedEndpoint` when
   present, else the user's `agentBaseUrl`; the same effective URL drives the remote-endpoint notice,
   so a fixed cloud endpoint is correctly recognised as remote.

3. **The key field + its "get a key" link follow `needsKey`.** The cloud providers require a key, so
   the field shows with a per-provider `Get an API key` link (`keyUrl`). The OpenAI-compatible
   branch keeps the field as well, but **optional** ("leave blank for local") — it covers both keyless
   local servers and keyed remote ones, and the app cannot tell which the endpoint is. Keys continue
   to flow through `ISecretStore` and **never** touch `QSettings` (ADR 0019).

4. **The model field is a filtered autocomplete, never a cage.** An editable combo seeded from the
   descriptor's `knownModels` suggests sensible per-provider models, but **free text is always
   accepted** so a stale bundled list never blocks the user. The list is a **bundled static seed**
   for now; live-fetching each provider's model list (`/models`, `/api/tags`) is a deliberate
   follow-up. A **conservative capability warning** appears only when journal-photo sending is on and
   the chosen model is a *known* text-only one (`textOnlyModels`); an unknown or free-text model never
   warns, so the hint has no false positives.

5. **Featured paths sit on top of the descriptor.** Two one-tap presets — **Set up free Google
   Gemini** (the hero: free tier, native dialect, tools + vision) and **Use local Ollama**
   (local/private, no key) — set the provider + default model and, for Gemini, open the key page and
   focus the key field. A short **guided wizard** offers the same choices as a step-through for users
   who prefer hand-holding; it writes the same settings + key seam the inline fields use, adding no
   backend. OpenAI / Anthropic / OpenRouter stay fully supported in the now-adaptive fields but get
   no preset/hero treatment.

6. **Honesty copy links out rather than hardcoding.** The free-tier note links to the provider's
   own rate-limit page instead of baking in numbers that shift, and the existing non-localhost data
   disclosure (notes/photos leave the device) is reused, not duplicated.

## Implementation order (one commit each, `ctest` green at each)

1. This ADR (docs-only).
2. `providerdescriptor.{h,cpp}` — the struct + `providerDescriptor(int)` table (+
   `test_providerdescriptor`: exact `fixedEndpoint` per provider; `defaultModel` ∈ `knownModels`;
   `keyUrl` non-empty iff `needsKey`; only OpenAI-compatible has an empty `fixedEndpoint`; an
   out-of-range index clamps).
3. `AgentViewModel` — an `effectiveBaseUrl()` consulting the descriptor, used by both
   `ensureSession()` and `endpointIsRemote()`; a `Q_INVOKABLE QVariantMap providerDescriptor(int)`
   bridge for QML (+ `test_agentviewmodel`: per-provider effective base URL; a fixed cloud endpoint
   reads as remote even with the local `agentBaseUrl`).
4. `AiSettings.qml` — adaptive endpoint/key visibility, the key link, the model autocomplete combo.
5. `AiSettings.qml` — the conservative capability warning under the model field.
6. `AiSettings.qml` — the Gemini + Ollama presets and the free-tier / disclosure copy.
7. `AiSetupWizard.qml` — the guided step-through, launched from the AI settings page.
8. Set this ADR's status accepted.

## Tests

`test_providerdescriptor` (pure: the four descriptors' endpoints, key URLs, defaults and model-list
invariants; the clamp), `test_agentviewmodel` (the effective base URL per provider type; the
remote-endpoint notice for a fixed cloud endpoint). The QML (adaptive fields, autocomplete, presets,
wizard) stays qmllint/cachegen + offscreen-smoke by design, consistent with the other settings
screens.

## Out of scope / follow-ups

- **Live model-list fetch** — `knownModels` is a bundled static seed; querying each provider's model
  endpoint (and filtering the combo against it) is a deliberate follow-up, gated on free text always
  being accepted so a stale seed never blocks anyone.
- **Per-model capability metadata** — the capability warning is conservative (known text-only models
  only) precisely because the descriptor carries no full per-model capability matrix yet. A richer
  matrix (vision/tools/reasoning per model) is a later refinement.
- **A user-editable provider table** — the descriptor is compile-time; making it user-extensible is
  not a need today (the OpenAI-compatible branch already absorbs arbitrary endpoints).
