# Klorophylle branding assets

Vector logo for the app. Concept: a stylised **K** — a leaf-green stem, a cyan
water/AI "swoosh" forming the upper arm, a Dark-Emerald base curve, and a veined
leaf. The editable master is [`../klorophylle.svg`](../klorophylle.svg)
(Inkscape); `mark.svg` is its cleaned, tightly-cropped export (hidden
trace raster removed). Regenerate the assets below with the build script if the
master changes.

## Files

| File | Use | Font-dependent? |
|------|-----|-----------------|
| `mark.svg` | Brandmark only (no wordmark) — toolbars, About header, watermark | No (no text) |
| `icon.svg` | App / launcher icon — full-colour mark on a light cyan-white rounded square; legible to 64px | No (no text) |
| `lockup.svg` | Full logo (mark + wordmark). **Editable source** — wordmark references the `Montserrat` family + `font-weight`. | Yes — needs Montserrat |
| `lockup-outlined.svg` | Full logo with the wordmark converted to paths. **Use this to ship/embed** — renders identically with no font installed. | No |

The wordmark uses **Montserrat** (the app's `Theme.fontDisplay`, bundled in
`src/style/fonts/`). Edit `lockup.svg`; re-export `lockup-outlined.svg` by converting
text to paths (e.g. `inkscape lockup.svg --export-text-to-path --export-plain-svg -o lockup-outlined.svg`)
with Montserrat resolvable.

## Palette

The logo is locked to the **Chlorophyll Intelligence** design system — the same canonical
hexes the `ThemeController` tokens use (`klorophylle/src/style/tokens.h`,
`the design system`). Do not introduce off-brand colours here; if the design
system changes, re-tint these SVGs to match.

| Brand role (design-system token) | Hex | Logo use |
|----------------------------------|-----|----------|
| Dark Emerald (`primary-container`) | `#004d36` | K base curve; KLOROPHYLLE wordmark uses `primary` `#003423` |
| Leaf Green (`secondary`/`good`) | `#39a339` | K stem + leaf; PLANT MONITOR subtitle |
| Cyan (`tertiary`/`ai`) | `#00cfff` | water/AI swoosh (the K's upper arm — hydration = cyan) |
| Background (light cyan-white) | `#f2fbfb` | `icon.svg` background |
| Outline-variant | `#bfc9c2` | `icon.svg` 1px border |

The leaf veins are holes in the leaf path — they show the surface behind the mark.
