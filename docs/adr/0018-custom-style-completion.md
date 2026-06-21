# ADR 0018 — Custom style completion: full control set + runtime style selection

**Status:** accepted · **Builds on:** ADR 0007 (the one `klr_style` module), ADR 0012
(themed controls, slice B), ADR 0013 (elevation/signature elements).

The earlier styling work themed most controls but left a small, enumerated set falling through to a built-in style:
`Switch` (Settings), `RadioButton` (Duplicate dialog), `Menu`/`MenuItem` (NavRail "More"),
`BusyIndicator` (scan/live/settings), the attached `ToolTip` (ScanScreen), plus one stray
`ItemDelegate` (NavRail). This ADR closes that gap — the final, previously-postponed bespoke
`klr_style` work — so **every control the app renders is our own**, and changes the style-selection model
from **compile-time to runtime** so that even *implicit/attached* controls (the attached `ToolTip`)
are drawn by our style. **No domain logic; no token changes.**

## Decisions

1. **Theme the remaining controls following the ADR 0012 pattern.** New files in `klr_style`:
   `Switch.qml`, `RadioButton.qml`, `Menu.qml`, `MenuItem.qml`, `BusyIndicator.qml`, `ToolTip.qml`.
   Each roots on `QtQuick.Templates` (NEVER `QtQuick.Controls` — self-derivation breaks the QML
   compiler), pulls every colour/size/radius/font from `Theme`, and expresses interaction states as
   soft `Qt.rgba(Theme.colorPrimary, α)` tints (~0.06 hover / 0.08–0.10 highlight / 0.12 press),
   matching the existing controls. `Switch` fills Leaf-Green when on; `RadioButton` is a Dark-Emerald
   ring + dot; `Menu` reuses `Dialog`'s card chrome (1px outline + `RectangularShadow`) with
   `MenuItem` rows tinted like `ListItem`. `BusyIndicator` is **emerald/primary, NOT cyan** — cyan
   (`colorAI`) is reserved for AI/sensor accents (brand rule, ADR 0013 #5), and a generic spinner is
   neither. The stray NavRail `RailButton` is rebased `ItemDelegate`→`ListItem` (it already overrode
   `background`/`contentItem`, so it's mechanical) to drop the last Material-derived base type.

2. **Switch from compile-time to RUNTIME style selection — this is the crux.** The previous setup
   (ADR 0007) was *compile-time style selection*, triggered because the style's qmldir imported a
   concrete built-in style (`QtQuick.Controls.Basic`) before `QtQuick.Controls`. In that mode Qt pins
   the style for implicit/attached controls and **`QQuickStyle::setStyle` is a no-op** (verified
   against Qt 6.11 docs *and* empirically: env var / `qtquickcontrols2.conf` `Style=` / `setStyle`
   were all ignored, the attached `ToolTip` stayed `Basic`). So our explicit controls were themed
   (direct module import) but the attached `ToolTip` could never be. To theme the attached form we
   move to **runtime selection**: `main.cpp` calls `QQuickStyle::setStyle("Klorophylle.Style")` +
   `setFallbackStyle("Basic")` before loading QML (the style name is the module's directory path
   `Klorophylle/Style`, dots→slashes; the qmldir's `module Klorophylle.Style` + statically-linked
   `klr_styleplugin` resolve it as a *custom* style, `custom=true`). Crucially the style's qmldir
   imports **no** Controls module: a concrete `QtQuick.Controls.Basic` would re-trigger compile-time
   selection, and the *generic* `QtQuick.Controls` would — while resolving the active style — dispatch
   right back to this same style, an infinite loop that hangs root construction (the window never
   maps). So the fallback is wired purely in code (`setFallbackStyle`), and the style files reference
   only `QtQuick.Templates` + their own siblings.

2a. **Controls our style files need that aren't in Templates are provided as siblings.** `Menu`/
   `ComboBox` use a scroll indicator; with no qmldir Controls import, `ScrollIndicator` must resolve
   without one. So the style ships its own **`ScrollIndicator.qml`** (referenced as a sibling via the
   implicit directory import) and uses `T.ScrollIndicator.vertical` (Templates) for the attached
   property — exactly how the built-in styles do it.

3. **Compile-time optimisation of OUR controls is preserved.** Screens still `import Klorophylle.Style`
   directly, so `Button`/`Switch`/… resolve to our QML at compile time (qmlcachegen AOT) regardless of
   the style-selection mode. Runtime selection only governs the *fallback/attached* chain. So we keep
   both: compile-time-resolved bespoke controls **and** a runtime style that backs the attached
   `ToolTip` and any unimplemented control (`StackView`/`Pane`/`ApplicationWindow` → Basic fallback).

4. **Prepend Qt's own QML import dir at startup to fix plugin resolution.** Qt's `QuickControls2` lib
   embeds a `:/qt-project.org/imports/QtQuick/Controls` qmldir that names a *dynamic* plugin; from our
   qrc-resident QML that stub is consulted first under runtime resolution and aborts with *"plugin
   qtquickcontrols2plugin not found"* (compile-time selection had bypassed runtime resolution, hiding
   this). `engine.addImportPath(QLibraryInfo::path(QLibraryInfo::QmlImportsPath))` gives the real
   installed (dev) / deployed plugin priority, fixing resolution portably. No explicit plugin links or
   `qt_import_qml_plugins()` are needed.

5. **Screens that use non-styled types import `QtQuick.Controls` themselves.** Because the style qmldir
   imports no Controls module, types our style doesn't provide no longer reach screens transitively, so
   the three screens that use them add an explicit `import QtQuick.Controls` (placed before
   `import Klorophylle.Style`, which still shadows the styled names): **Main** (`ApplicationWindow`/
   `SplitView`/`StackView`), **ScanScreen** (the attached `ToolTip` type), **DuplicateDialog**
   (`ButtonGroup`). ScanScreen keeps the idiomatic attached form (`ToolTip.visible`/`ToolTip.text`);
   under runtime selection its shared delegate is the active style's `ToolTip.qml` — i.e. ours.

6. **`qtquickcontrols2.conf` is removed.** It only carried the old Material fallback palette and a
   `Style=` line that would conflict with the code `setStyle`. Style/fallback are now the single
   source of truth in `main.cpp`; Basic is Qt's default custom-style fallback regardless.

## Implementation order (one commit each, tests green at each)

1. `Switch.qml` + `RadioButton.qml`; NavRail `RailButton` `ItemDelegate`→`ListItem`.
2. `Menu.qml` + `MenuItem.qml`.
3. `BusyIndicator.qml`, `ToolTip.qml`, `ScrollIndicator.qml`.
4. Runtime selection: `main.cpp` `setStyle`/`setFallbackStyle` + `QLibraryInfo` import-path prepend;
   style qmldir imports **no** Controls module; `Menu`/`ComboBox` use `T.ScrollIndicator.vertical` +
   the sibling `ScrollIndicator`; `import QtQuick.Controls` added to Main/ScanScreen/DuplicateDialog;
   remove `qtquickcontrols2.conf`.
5. This ADR.

## Verification

- Build + `ctest` green (46/46, offscreen). The generated `Klorophylle/Style/qmldir` has no
  `QtQuick.Controls` import; no active `Material` references remain in `src`.
- App run on a **real X11 display**: the main window maps at 1180×760 (`visible`, `Windowed`), the
  style resolves `custom=true` as `Klorophylle.Style`, and both the Plants home and the Sensors screen
  (with its attached `ToolTip`) load with no QML errors. (The earlier "process starts, nothing opens"
  was a load-failure chain — a style file using `ScrollIndicator` with no resolving import — surfaced
  only on a real windowing platform; `offscreen` had masked it.)

## Not done (follow-ups)

- **Visual confirmation** of every new control (and the attached tooltip) needs a real GUI run — the
  offscreen checks prove resolution + no errors, not pixels.
- **Unused controls** (`CheckBox`, `Slider`, `SpinBox`, `ScrollBar`, …) are intentionally not themed —
  they degrade to neutral Basic until a screen needs one; theme on first use.
- No CI screen-instantiation smoke yet (consistent with ADR 0012/0013; rely on `qmlcachegen` AOT +
  `all_qmllint` + `ctest`).
