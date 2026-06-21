# ADR 0025 — Vision: per-dialect image plumbing + the `read_plant_photo` tool

**Status:** accepted · **Builds on:** ADR 0019 (the agent
harness + dialects), ADR 0024 (journal attachments), ADR 0023 (the opt-in-setting gating precedent).

This milestone makes a vision-capable model *see* a plant's journal photos. Two halves: teach the four provider
dialects to encode images (until now a hard `rejectImages()` error), and add a read-only
`read_plant_photo` tool that returns a plant's photos as image content. Scope: **photos in
the journal only** — no chat composer that attaches photos to a user message; images reach the model
solely via the tool. Privacy: the pre-send disclosure shows **thumbnails** of every image leaving the
device.

## Decisions

1. **The dialects encode images unconditionally; `karness` learns nothing about plants or settings.**
   Each codec's `rejectImages()` is replaced with real per-provider encoding:
   - **Chat Completions** — user content becomes a `[{type:text},{type:image_url,image_url:{url:"data:…"}}]`
     array. A tool message is **text-only**, so images a tool returned are **hoisted** into one
     following `{role:"user"}` message of `image_url` parts (the tool-result→user-block workaround,
     decision 5 of ADR 0019). Image-only tool results get a `"[see image below]"` anchor so the tool
     message is non-empty.
   - **Anthropic** — `{type:image, source:{type:base64, media_type, data}}`, valid both on a user turn
     and **inside a `tool_result`**'s content (so no hoist needed).
   - **Gemini** — `{inlineData:{mimeType, data}}` parts, on the user turn and as **sibling parts** of a
     `functionResponse` (the response object carries no image).
   - **Responses** — `{type:input_image, image_url:"data:…"}` on a user turn; tool-result images are
     **hoisted** onto a following user input item (the well-defined `input_image` form, avoiding the
     uncertain image-in-`function_call_output` shape).
   `MessageAccumulator` is unchanged — images are input-only, never streamed as deltas. `karness`
   still builds standalone (the extraction gate).

2. **Vision is an explicit opt-in setting, off by default.** The user picks the model, so vision can't
   be auto-detected — `SettingsStore::agentVisionEnabled` (key `agent/visionEnabled`, default false,
   `agentChanged` → session rebuild) mirrors the web-tool precedent. It drives three things together:
   `ModelCaps.vision`, `read_plant_photo` registration, and the system-prompt mention.

3. **`read_plant_photo` is read-only, registered only when `agentToolsEnabled && agentVisionEnabled`.**
   It returns a plant's most-recent journal photos as a `ToolOutcome` whose parts interleave a
   `TextBlock` (caption/date context) and an `ImageBlock` (bytes from the file store, MIME from the
   file extension). Because the tool is registered only when vision is on, an `ImageBlock` can never
   reach a dialect while vision is off — so the dialects' unconditional encoding (decision 1) needs no
   runtime gate; the enforcement is structural. (Turning vision off later does **not** redact images
   already in a stored transcript — the user consented when the tool ran; the toggle gates *new*
   acquisition.)

4. **The transcript renders returned images; the pre-send disclosure shows their thumbnails.** The
   chat view-model gains an `imagesData` role (a list of `data:` URLs) so a tool-result row renders
   thumbnails instead of the old `"[image]"` placeholder. The pre-send disclosure (shown before the
   first send to a non-localhost endpoint) gains an `outgoingImages` list bound to a thumbnail panel —
   the one place this milestone extends rather than reuses the existing disclosure, since images only appear *after* a
   `read_plant_photo` call mid-turn.

## Implementation order (one commit each, ctest green throughout)

1. This ADR.
2. Anthropic codec: image source block on user + tool_result; remove `rejectImages` (+ provider test).
3. Gemini codec: `inlineData` on user + tool response; remove `rejectImages` (+ provider test).
4. Responses codec: `input_image` on user + tool-result hoist; remove `rejectImages` (+ provider test).
5. Chat Completions codec: `image_url` on user + the tool-result→user hoist; remove `rejectImages`
   (+ codec hoist test).
6. `SettingsStore::agentVisionEnabled` (+ `test_settingsstore`).
7. `ReadPlantPhotoTool` over the attachment repo + file store (+ `test_agenttools`).
8. `AgentViewModel`: own + register the tool gated on vision; `caps.vision` from the setting; name it
   in the instructions (+ `test_agentviewmodel`).
9. Settings UI: the vision toggle.
10. Chat image rendering (`imagesData` role + QML thumbnails) + the disclosure thumbnail panel
    (`outgoingImages`).
11. Flip the vision milestone done in ADR 0019.

## Tests

`test_chatcompletionscodec` (user image_url; tool-result hoist + multi-result coalescing),
`test_anthropicprovider` / `test_responsesprovider` / `test_geminiprovider` (an image request encodes
the provider's wire form + base64 and streams to done), `test_settingsstore` (`agentVisionEnabled`
default/persist), `test_agenttools` (`read_plant_photo` returns image parts; bad id; no photos; limit),
`test_agentviewmodel` (vision-gated registration; `caps.vision`; image row renders; `outgoingImages`).
The QML thumbnail rendering + disclosure panel stay qmllint/cachegen-only by design.

## Out of scope / follow-ups

- **Chat-attached photos** (a composer that attaches an image to a user message) — deliberately not in
  this milestone (photos in the journal only). The dialect plumbing already supports a user-turn image, so
  this is a later UI-only addition.
- **Image-in-`function_call_output` for Responses** — using the hoist instead until the wire shape is
  confirmed against the live API.
- **Thumbnail downscaling before send** — images are sent at stored resolution; a resize/recompress
  step (to cut tokens/bandwidth) is a later optimization.
