# ADR 0012 — Themed controls: killing the Material leaks (slice B)

**Status:** accepted · **Builds on:** ADR 0007 (the `klr_style`
module + the Material-fallback mechanism this extends).

ADR 0007 stood up `Klorophylle.Style` with tokens (`Theme`), the presentation seam
(`Format`), and a *skeleton* set of control overrides (`Button`, `Label`, `ToolButton`,
`ToolBar`, plus the gap-fillers `Card`/`SectionHeader`/`StatusPill`/`ProgressBar`). Everything
else fell through the qmldir's `import QtQuick.Controls.Material` fallback. Slice A added the
rich `PlantCard`. **Slice B closes the gap on the controls users actually touch** — `ComboBox`,
`TextField`, `Dialog` (+ `DialogButtonBox`), `TabBar`/`TabButton`, and list rows — which until
now rendered as Material (recoloured by `app/qtquickcontrols2.conf`, but still Material's shape,
chrome, ripple and interaction). This ADR records the wrapper conventions and the slice's
deliberate boundaries; it adds **no domain logic, no C++ behaviour change, and no new `Theme`
tokens** (elevation + the gradient/scrim tokens are slice C).

## Decisions

1. **Wrappers root on `QtQuick.Templates`, never `QtQuick.Controls`.** Each new wrapper extends
   the `T.*` template type (rooting on the `Controls` type would self-derive and break the QML
   compiler), overrides only the structural seams (`contentItem`/`background`/`indicator`/
   `popup`/`header`/`footer`), and binds the standard `implicitWidth/Height` inset+padding
   formula — exactly as `Button.qml`/`ToolButton.qml` already do. New files register by being
   listed in the `QML_FILES` block of `src/style/CMakeLists.txt`; the qmldir is auto-generated,
   so the wrapper *replaces* the Material fallback for that type with no other wiring.

2. **Every colour comes from `Theme`; interaction states are `Qt.rgba` tints.** No hardcoded
   hexes. Hover/press/highlight are soft `Qt.rgba(Theme.colorPrimary.r, .g, .b, α)` washes
   (≈0.06 hover / 0.10 highlight / 0.12 press), matching the existing `Button`/`ToolButton`
   ripple-tint idiom — so no `surface-container` ramp token was needed this slice, and the
   controls re-theme live on Light↔Dark.

3. **`ListItem` is structural-only, not compositional.** The app's ten list rows compose wildly
   different content (a `StatusPill` + value, a delete `ToolButton`, a device line, a
   primary/error-tinted action row…). `ListItem` (a `T.ItemDelegate`) themes the row *chrome*
   only — padding + hover/press/`highlighted` background + no Material ripple — and leaves
   `contentItem` entirely to the caller. Screens migrate raw `ItemDelegate` rows to `ListItem`;
   it is a **new type name** (not a Material-type override), so the migration is an explicit
   rename, not an automatic reroute.

4. **The five same-named wrappers reroute automatically.** `ComboBox`/`TextField`/`Dialog`/
   `TabBar`/`TabButton` share the Material type names, and every screen already
   `import Klorophylle.Style` exclusively (ADR 0007), so adding the wrappers reroutes those call
   sites off the Material fallback with **zero source change**. The screen sweep only had to
   rename `ItemDelegate → ListItem` and verify rendering.

5. **`Dialog` carries a basic scrim now; the glass-greenhouse blur is slice C.** `Dialog` themes
   its background (card surface, 1px outline, `radiusMd`), header (title in the Montserrat
   authority ramp), and footer (a `DialogButtonBox` whose buttons resolve to our themed
   `Button`). The modal dim is a plain `Qt.rgba(colorPrimary, 0.32)` scrim set via
   `T.Overlay.modal`/`modeless`. The **formal backdrop-scrim token, elevation/shadow, and the
   ~12px backdrop blur are explicitly deferred to slice C** (the design system § Elevation).

6. **What stays on Material by design.** `RadioButton`/`CheckBox`/`Switch` (the design system:
   toggles/checkboxes are "standard Material-ish") and the native `FileDialog` (a platform
   dialog, not a styleable Templates type) keep the Material fallback. `Menu`/`MenuItem` are not
   in this slice. `NavRail`'s `RailButton` stays a raw `ItemDelegate`: it already fully overrides
   `contentItem` *and* `background` with `colorOnPrimary` tints for the dark nav surface, so no
   Material chrome leaks and `ListItem`'s emerald tints would be wrong there.

7. **No new gates this slice.** The planned no-hardcoded-hex CI grep and the
   offscreen per-screen smoke test are *not* added here; we keep relying on the existing
   build-time gates — `qmllint` (the `all_qmllint` target) + `qmlcachegen` AOT-compiling every
   screen — plus `ctest`. The no-hardcoded-hex rule is honoured by review. `test_theme` is
   unchanged because no tokens changed.

## Implementation order (one commit each, tests green at each)

1. `ComboBox.qml` — emerald field border (asserts on focus/open), `expand_more` indicator,
   themed popup + delegate rows.
2. `TextField.qml` — cyan-white fill, 1px emerald border with a soft focus-glow ring, no
   Material underline.
3. `TabBar.qml` + `TabButton.qml` — flat strip with a hairline baseline; checked tab in emerald
   with a 2px underline, no ripple.
4. `ListItem.qml` — structural `T.ItemDelegate` (themed hover/press/highlight chrome only).
5. `Dialog.qml` + `DialogButtonBox.qml` — card chrome, Montserrat title header, transparent
   themed footer, basic emerald scrim.
6. Screen sweep — rename `ItemDelegate → ListItem` across LiveScreen, ScanScreen,
   SpeciesPickerDialog, PlantDetailScreen (care + journal), PlantSettingsScreen (actions +
   sensor-pick); the same-named wrappers reroute the rest automatically.
7. This ADR.

## Not done in slice B (→ slice C)

- Elevation/shadow tokens, the 1px-outline rule as a token, and the backdrop-scrim token on
  `ThemeController`; the ~12px modal blur.
- Applying the gradient `ProgressBar` to the plant-detail readouts.
- The cyan AI pulsing-node indicator (parked until the AI surfaces exist).
