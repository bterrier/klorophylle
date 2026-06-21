# ADR 0013 — Elevation tokens & signature elements (slice C)

**Status:** accepted · **Builds on:** ADR 0007 (the `klr_style` tokens) and ADR 0012 (the themed controls this
adds depth to).

Slices A (rich `PlantCard`) and B (themed controls, ADR 0012) left the design system base-only
in three ways the components were faking inline: no **elevation** (flat cards/dialogs), an
**inline modal scrim** (`Dialog` hardcoded `Qt.rgba(colorPrimary, 0.32)`), and the gradient
`ProgressBar` only on `PlantCard` (the plant-detail readouts showed value text alone). Cyan
(`colorAI`) also had no reusable home. Slice C — the last of the styling track — adds the elevation + scrim
tokens, applies them, puts the gradient bar on the care readouts (one small GUI-model role), and
lands a minimal cyan pulsing-node. **No domain logic**; same token/test discipline as the earlier slices.

## Decisions

1. **Elevation is rendered with `RectangularShadow`, not `MultiEffect`.** `RectangularShadow`
   (`QtQuick.Effects`, Qt 6.9+; we run 6.11.1) is purpose-built for rounded-rect drop shadows —
   its API mirrors CSS `box-shadow` (`blur`/`color`/`offset`/`radius`/`spread`). It draws the
   shadow *directly behind* the surface with **no `layer.enabled`/`ShaderEffectSource`**, so it
   avoids the clipping- and auto-padding fragility of `layer.effect: MultiEffect` (the approach
   the legacy `thirdparty/ComponentLibrary` uses). `Card`/`Dialog`'s `background:` becomes an
   `Item` wrapping `[RectangularShadow, Rectangle]`.

2. **Shadow + scrim are FIXED Dark-Emerald tints in both schemes — not scheme-derived.** The
   spec ambient shadow is `0 10px 30px rgba(0,77,54,0.08)`. A shadow/scrim must *darken* the
   backdrop in **both** Light and Dark; deriving them from `primary` would break in Dark, where
   `primary` inverts to a light emerald. So `tokens.h` carries a fixed `kShadowTint = 0x004d36`
   (= `rgb(0,77,54)`) with `kShadowAlpha` (~0.08) and `kScrimAlpha` (~0.32), and `ThemeController`
   exposes `colorShadow`/`colorBackdropScrim` as **CONSTANT** (not `colorsChanged`) QColors plus
   the scalar `elevationOffsetY` (10) / `elevationBlur` (30). All four are asserted in `test_theme`
   (values + scheme-independence).

3. **No new gradient colour token.** The bar's endpoints are already `Theme.colorGood`→
   `Theme.colorAI` (tokens, asserted by the palette tests from the start). The exit criterion's
   "gradient tokens" is met by that existing pair; inventing a third would be redundant. The
   track tint stays `Qt.rgba(colorGood, 0.12)` — a token tint, not a hardcoded hex.

4. **The plant-detail gradient bar needs a model fraction, computed like the home cards.**
   `PlantCareModel` gains `FractionRole` + `HasRangeRole`, computed from the cached canonical
   ranges exactly as `PlantListModel::metricOf` does — `clamp01((v−min)/(max−min))`, both bounds
   required. Canonical value ÷ canonical bounds ⇒ a unit-independent ratio (no conversion). The
   CARE delegate shows a `ProgressBar` under each row when `hasRange && present`.

5. **`PulsingNode` lands now, unused.** The cyan AI/sensor accent (the logo's neural-node motif)
   is the *one* reusable home for `colorAI` as an indicator, so the brand's "cyan = AI/sensor
   only" rule is encapsulated and cyan can't leak into generic chrome. Its consumers (the AI
   surfaces) arrive with the AI work; the component is minimal and self-contained until then.

6. **Backdrop blur is deferred — not just unimplemented, but flagged as ill-fit for QML.** The
   spec's ~12px "glass greenhouse" blur behind modals needs the overlay to *sample the content
   behind it*; QML has no clean way to do that without the front Item being handed the background
   Item to blur via a `ShaderEffectSource` — a coupling that is the wrong shape for a generic
   `Dialog`. Slice C ships the formal scrim token only; the blur is a recorded follow-up to
   investigate (here and in the design system), not a committed deliverable.

## Implementation order (one commit each, tests green at each)

1. `colorShadow`/`colorBackdropScrim`/`elevationOffsetY`/`elevationBlur` on `ThemeController`
   (+ `tokens.h` constants) + `test_theme` assertions.
2. `RectangularShadow` behind `Card`/`Dialog`; `Dialog` scrim → `Theme.colorBackdropScrim`.
3. `PlantCareModel` `FractionRole`/`HasRangeRole` + `test_plantcaremodel`.
4. `PlantDetailScreen` CARE delegate → gradient `ProgressBar` under each ranged row.
5. `PulsingNode.qml` in `klr_style` (added to `QML_FILES`).
6. This ADR.

## Not done (follow-ups)

- **Backdrop blur** behind modals (decision 6) — investigate a non-coupled approach later.
- `PulsingNode` has no screen consumer until the AI surfaces land.
- No CI grep / offscreen smoke test (consistent with slice B; rely on `qmlcachegen` AOT +
  `all_qmllint` + `ctest`).
