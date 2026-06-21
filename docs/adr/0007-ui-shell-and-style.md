# ADR 0007 — UI shell & style: tokens + a compile-time custom style

**Status:** accepted · **Builds on:** ADR 0002 (QML-singleton injection),
ADR 0006 (the screens this restyles).

This phase brings the *"Chlorophyll Intelligence"* design system into code as a single source of
colour/type/spacing and a themed shell, so every later screen is built styled rather than
retrofitted. The token **values** are specified in `the design system`; this ADR records the
**structure and mechanism** chosen to host them.

This deviates from the original "tokens on a built-in style, custom style postponed" plan in one respect:
we stand up a **skeleton custom Qt Quick Controls style from day one**
that **falls back to Material**, because the foundation is identical either way and a skeleton makes
the eventual custom-control work purely additive.

## Decisions

1. **One module, not three.** Tokens, the Qt Quick Controls style, and generic gap-filler components
   all live in **one** module `Klorophylle.Style` (backing lib `klr_style`). Everything that touches
   the theme also touches the style, so there is no seam worth cutting between them. App-*specific*
   composites (e.g. a future `PlantCard`) get their own `Klorophylle.Components` module later, which
   imports `Klorophylle.Style`. The custom-control overrides flesh out *inside* `klr_style` over time —
   additive, no new module. Layering stays acyclic and downward: `klr_core → klr_style → klr_gui → app`
   (`klr_style` depends only on `klr_core` for enums/formatters + Qt).

2. **Why no token/style cycle.** A component does **not** import the style to get a styled control —
   the Qt Quick Controls style mechanism is inversion-of-control: a component instantiates the generic
   `Button`, and the *active style* supplies the look at resolution time. So tokens are a pure base
   both the style and components read; the component↔style link is selection, not a compile edge. A
   cycle is only possible if the style imported the components — which it never does (style impls are
   leaf-level: `QtQuick.Templates` + tokens only).

3. **Compile-time style selection.** Screens `import Klorophylle.Style` (before any other Controls
   import); `main.cpp` sets **no** runtime style. This drops the runtime style-selection plugin and
   lets the QML compiler codegen control bindings — appropriate for our single Linux-desktop target
   where runtime style-switching is a non-goal (per the UI design).

4. **Material fallback via the qmldir.** Controls we don't implement resolve to **Material**. The
   fallback is declared in the style's qmldir as an `import QtQuick.Controls.Material` line, emitted by
   `qt_add_qml_module(... IMPORTS QtQuick.Controls.Material ...)` — no hand-authored qmldir. **Gotcha:**
   we must **not** use `/auto` there: `auto` would propagate *this* module's version (1.0) to Material,
   requesting the non-existent "Material 1.0". Omitting the version imports the greatest available.
   (Built-in styles can use `auto` only because their own version is 6.x.)

5. **Tokens in `ThemeController` (QML `Theme`), values in `tokens.h`.** Token values are `constexpr`
   tables in `tokens.h`, unit-tested in `test_theme` against `the design system` — never hand-copied
   hexes in QML. `Theme` exposes them as scheme-aware `QColor`/size/spacing/radius
   properties and owns `ColorScheme {Light,Dark,Auto}` (Auto follows the OS via `QStyleHints`). It is
   the **single colour owner**: semantic `colorGood/Warn/Bad/AI` live here (not split onto `Format`),
   avoiding cross-singleton coupling. The **`warn` amber** and the **dark scheme** are *derived* from
   the seeds (both absent from the export — `the design system` open questions) and flagged for revisit.

6. **`Format` is the presentation seam.** A `QML_SINGLETON` wrapping the unit-tested `klr_core`
   formatters (`label`/`unitSymbol`) for QML. It is where enum/status→label+colour and unit
   conversions will land in later features. This phase keeps it thin. (`journalformat` stays in `klr_gui` for now: moving it
   into `klr_style` would pull a `klr_persistence` dependency — `JournalEntryKind` lives there — which
   would violate "style depends only on core". Revisit if the enum moves down.)

7. **Material fallback palette is static.** Because QML must never import two styles, the fallback
   Material palette is configured via `:/qtquickcontrols2.conf` `[Material]`, matched to the LIGHT
   tokens. Consequence: our own controls + all Theme-bound content re-theme live (incl. Light↔Dark),
   but fallback-rendered Material controls follow the conf. As more controls are implemented in
   `klr_style`, less falls back. Acceptable for a skeleton; documented limitation.

8. **Custom components derive from templates.** `Card` → `T.Pane`, `StatusPill` → `T.Control`,
   `SectionHeader` → the style's `Label`, each carrying the standard
   `implicitWidth/Height = Math.max(background, content+padding)` sizing so they behave like real
   controls — never bare `Item`/`Rectangle`.

9. **Fonts: one variable TTF per family.** Montserrat + Inter (SIL OFL) ship as their **variable**
   upright fonts in `fonts/`, bundled as `klr_style` resources under `:/klr/fonts/`. A variable font
   registers under the bare family name ("Montserrat" / "Inter"); the per-optical-size *static* Inter
   files would instead register as "Inter 18pt" etc. and break `font.family: "Inter"`, so we avoid
   them. `ThemeController` registers both via `QFontDatabase::addApplicationFont` once at first
   construction (no-op without a `QGuiApplication`, so guiless tests are unaffected). `font.weight`
   selects the weight off the `wght` axis.

10. **Icons: an `Icon` component over a glyph font.** Material Symbols (Outlined, variable;
    Apache-2.0) is bundled like the brand fonts and exposed as `Theme.fontIcon`. The app's one icon
    primitive is `Icon.qml`, which renders a Material Symbols **glyph** when `icon.name` (a ligature,
    e.g. `delete`) is set, or an **Image** when `icon.source` (e.g. an SVG) is set. The glyph `Text`
    **must** use `renderType: Text.NativeRendering` — the default distance-field renderer mangles
    large/variable icon glyphs. `icon` is a grouped property (`name`/`source`/`color`/`size`) mirroring
    Qt's control `icon` API, but typed by **our** `IconInfo` (a `QObject` value) on a C++ `IconBase :
    QQuickItem` base, because Qt's `QQuickIcon` is private. `Icon.qml` derives from `IconBase`. Used via
    standalone. For controls, the style's `ToolButton`/`Button` bridge the control's **built-in** `icon`
    grouped property (Qt's `QQuickIcon`) to our `Icon` renderer in their `contentItem`, so call sites
    write `ToolButton { icon.name: "delete" }` — never an Icon `contentItem` override. (Full SVG tinting
    and the complete icon rollout are follow-ups; the 10.6 MB full variable font is unsubsetted for now.)

11. **The dark "authority" surface is the NavRail sidebar.** On the desktop shell the brand
    `colorPrimary` (Dark Emerald) + `colorOnPrimary` (white) treatment lives on the **left `NavRail`**
    (the nav container per the design system), not the top bar. The content header `ToolBar` is a light
    surface (back + page title). Because the flat style has no automatic foreground propagation
    (Material's `foreground` attached property), on-dark children opt in via a small explicit seam:
    `ToolButton` exposes a `contentColor` (default `colorPrimary`, correct on light screens) that tints
    its icon/text/ripple; the rail's buttons/labels use `colorOnPrimary`. Scheme-correct both ways
    (Dark: light-emerald rail / dark content). *(The top bar was briefly the dark authority bar before
    the sidebar shell landed; the authority treatment moved to the rail with the desktop IA.)*

## Implementation order (one commit each, tests green at each)

1. `klr_style` lib + `Klorophylle.Style` module (C++ singletons only); exe links the plugin.
2. `ThemeController` (`Theme`) tokens in `tokens.h` + `test_theme` (LIGHT palette, type, spacing).
3. Derived `warn` amber + dark scheme; `ColorScheme` switching (covered by `test_theme`).
4. `Format` seam wrapping the core formatters.
5. Skeleton style: `Button.qml`/`Label.qml` (Templates-rooted) + `IMPORTS QtQuick.Controls.Material`
   fallback; drop `QQuickStyle::setStyle`; add the Material `qtquickcontrols2.conf`.
6. Generic components `Card`/`SectionHeader`/`StatusPill`; bundle the variable fonts.
7. Migrate the five screens to `import Klorophylle.Style` and restyle off tokens/components
   (no hardcoded hexes/sizes/magic margins remain).
8. This ADR.

## Not done in this phase (follow-ups)

- Full per-control style set (the postponed `klr_style` fleshing-out); a screen-instantiation QML smoke
  test (today qmlcachegen AOT-compiles every screen at build time, which already catches import/type
  errors).
- Icons: the `Icon` component + Material Symbols font exist and the placeholder glyphs (`🗑`/`‹`/`›`)
  are replaced, but a full icon rollout, **SVG tinting** in the Image branch (needs a `MultiEffect`),
  and **subsetting** the 10.6 MB font to used glyphs are follow-ups.
- Only the **upright** variable fonts are bundled (no italics); the per-weight static files are
  intentionally omitted in favour of the variable axis.
