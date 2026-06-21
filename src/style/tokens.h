// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGui/QColor>

// The *Chlorophyll Intelligence* design tokens as a single, unit-tested source of
// truth (the design system). NEVER hand-copied hexes in QML
// — instead QML binds to ThemeController, which reads
// these tables. Colours are kept as constexpr 0xRRGGBB literals so the table is a
// compile-time constant a test can assert against; QColor is materialised on read.
namespace klr::tokens {

// One colour scheme's worth of role values. Names follow the Material-3 role set the
// Stitch export uses (the design system), reduced to the roles the app actually binds.
struct Palette {
    QRgb background;     // app canvas
    QRgb surface;        // == background for our flat canvas
    QRgb card;           // surface-container-lowest — pure white over the cyan-white bg
    QRgb cardBorder;     // outline-variant — low-contrast 1px card border
    QRgb text;           // on-surface — body text
    QRgb textVariant;    // on-surface-variant — secondary text
    QRgb outline;        // outline
    QRgb primary;        // Dark Emerald — typography/nav/branding authority
    QRgb onPrimary;      // text on primary
    QRgb good;           // semantic: healthy / affirmative (Leaf Green)
    QRgb warn;           // semantic: requires attention (derived amber — no export role)
    QRgb bad;            // semantic: error / alert
    QRgb ai;             // AI + hydration/sensor accent (Cyan) — Format/Theme.colorAI ONLY
};

// LIGHT — taken verbatim from the design system "Color tokens (Material-3 role set,
// LIGHT)". `warn` has no export role; #f5a623 is a derived amber sitting between Leaf
// Green and Error (the design system open question #1).
inline constexpr Palette kLight {
    .background  = 0xf2fbfb,
    .surface     = 0xf2fbfb,
    .card        = 0xffffff,
    .cardBorder  = 0xbfc9c2,
    .text        = 0x151d1d,
    .textVariant = 0x404944,
    .outline     = 0x707973,
    .primary     = 0x003423,
    .onPrimary   = 0xffffff,
    .good        = 0x39a339,
    .warn        = 0xf5a623,
    .bad         = 0xba1a1a,
    .ai          = 0x00cfff,
};

// DARK — not in the export (the design system open question #2). Derived from the same
// seeds: an emerald-tinted near-black canvas, the `inverse-on-surface` light text, the
// `inverse-primary` / `*-fixed` lifts for primary, and lighter accents that read on a
// dark surface. Revisit when a proper dark exploration lands.
inline constexpr Palette kDark {
    .background  = 0x0e1614,
    .surface     = 0x0e1614,
    .card        = 0x18211e,
    .cardBorder  = 0x2b3531,
    .text        = 0xe9f2f2,
    .textVariant = 0xb8c3bd,
    .outline     = 0x8a938d,
    .primary     = 0x93d4b5,  // inverse-primary (a light emerald)
    .onPrimary   = 0x003523, // dark text on the light primary
    .good        = 0x8ffb84,  // secondary-container
    .warn        = 0xffc14d,
    .bad         = 0xffb4ab,  // typical M3 dark error
    .ai          = 0xb8eaff,  // tertiary-fixed
};

// Elevation & depth (the design system). The soft ambient shadow and the modal
// backdrop scrim are a FIXED Dark-Emerald tint in BOTH schemes — rgb(0,77,54) = 0x004d36.
// They are deliberately NOT scheme-derived from `primary`: a scrim/shadow must darken the
// backdrop in dark mode too, but `primary` inverts to a light emerald there. Spec ambient
// shadow: `0 10px 30px rgba(0,77,54,0.08)`.
inline constexpr QRgb kShadowTint = 0x004d36;
inline constexpr int kShadowAlpha = 20;   // ~0.08 * 255 — the ambient card/dialog shadow
inline constexpr int kScrimAlpha  = 82;   // ~0.32 * 255 — the modal backdrop dim
inline constexpr int kElevationOffsetY = 10; // px, the shadow's downward offset
inline constexpr int kElevationBlur    = 30; // px, the shadow's blur radius

} // namespace klr::tokens
