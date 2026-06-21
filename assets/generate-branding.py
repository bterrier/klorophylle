#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rebuild branding/{mark,icon,lockup}.svg from the Inkscape master logo.
#
#   master:  klorophylle.svg  (this dir) — hand-drawn in Inkscape; may carry a
#            hidden display:none trace raster, which is dropped here.
#
# Produces, all cropped tight to the visible drawing (bbox queried via inkscape):
#   mark.svg    transparent brand mark (no wordmark)
#   icon.svg    mark on the light cyan-white (#f2fbfb) rounded square — app/launcher icon
#   lockup.svg  mark + Montserrat "KLOROPHYLLE / PLANT MONITOR" wordmark (editable source)
#
# The full visible subtree (groups + paths, with their transforms) is preserved and
# re-placed by an OUTER transform — so it stays correct no matter how the master nests
# or translates its layers. Only the hidden <image> and Inkscape/sodipodi cruft are dropped.
#
# After running this:
#   1) re-export the self-contained lockup:
#        inkscape branding/lockup.svg --export-text-to-path --export-plain-svg \
#                 -o branding/lockup-outlined.svg     (with Montserrat resolvable)
#   2) regenerate the platform icon set:  ./generate-icons.sh
#
# Requires: inkscape (bbox query), Python 3.
import copy, os, subprocess, sys, xml.etree.ElementTree as ET

SVG = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG)

HERE = os.path.dirname(os.path.abspath(__file__))
SRC  = sys.argv[1] if len(sys.argv) > 1 else f"{HERE}/klorophylle.svg"
OUT  = f"{HERE}/branding"

def query(flag):
    return float(subprocess.run(["inkscape", SRC, flag], capture_output=True, text=True).stdout.strip())
X0, Y0, W, H = query("--query-x"), query("--query-y"), query("--query-width"), query("--query-height")
CX, CY, SIDE = X0 + W/2, Y0 + H/2, max(W, H)

# Deep-copy the root, drop the hidden raster + editor cruft, keep the rest (groups,
# paths, defs) with all their transforms intact, then serialise the visible children.
def clean(el):
    for child in list(el):
        t = child.tag.split('}')[-1]
        if t in ('image', 'namedview', 'metadata'):
            el.remove(child); continue
        for k in list(child.attrib):
            if 'inkscape' in k or 'sodipodi' in k:
                del child.attrib[k]
        clean(child)

root = copy.deepcopy(ET.parse(SRC).getroot())
clean(root)
inner = "".join(ET.tostring(c, encoding="unicode").strip() for c in root)

def placed(scale, tx, ty):
    return f'  <g transform="translate({tx:.3f},{ty:.3f}) scale({scale:.5f})">{inner}</g>'

# mark.svg — tight square viewBox in the master's rendered space (no outer transform)
pad = 22.0; side = SIDE + 2*pad; mnx, mny = CX - side/2, CY - side/2
open(f"{OUT}/mark.svg", "w").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<!-- Klorophylle brand mark. Generated from klorophylle.svg by '
    'assets/generate-branding.py. Chlorophyll Intelligence palette. -->\n'
    f'<svg width="512" height="512" viewBox="{mnx:.3f} {mny:.3f} {side:.3f} {side:.3f}"\n'
    f'     xmlns="{SVG}">{inner}</svg>\n')

# icon.svg — mark on the light rounded square, 96px padding
P = 96.0; sc = (512 - 2*P)/SIDE
open(f"{OUT}/icon.svg", "w").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<!-- App / launcher icon: brand mark on the light cyan-white surface (#f2fbfb). -->\n'
    f'<svg width="512" height="512" viewBox="0 0 512 512" xmlns="{SVG}">\n'
    '  <rect x="0" y="0" width="512" height="512" rx="112" fill="#f2fbfb"/>\n'
    '  <rect x="3" y="3" width="506" height="506" rx="109" fill="none" stroke="#bfc9c2" stroke-width="3"/>\n'
    f'{placed(sc, 256 - sc*CX, 256 - sc*CY)}\n</svg>\n')

# lockup.svg — mark (centred, top) + Montserrat wordmark
sc2 = 330.0/SIDE
open(f"{OUT}/lockup.svg", "w").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    f'<svg width="900" height="760" viewBox="0 0 900 760" xmlns="{SVG}">\n'
    f'{placed(sc2, 450 - sc2*CX, 210 - sc2*CY)}\n'
    '  <!-- Editable source: wordmark uses the Montserrat display family. For a\n'
    '       self-contained asset use lockup-outlined.svg (text converted to paths). -->\n'
    '  <text x="450" y="600" font-family="Montserrat" font-weight="700" font-size="92"\n'
    '        letter-spacing="1" text-anchor="middle" fill="#003423">KLOROPHYLLE</text>\n'
    '  <text x="450" y="663" font-family="Montserrat" font-weight="600" font-size="32"\n'
    '        letter-spacing="16" text-anchor="middle" fill="#39a339">PLANT MONITOR</text>\n'
    '</svg>\n')

print(f"branding rebuilt from {os.path.relpath(SRC, HERE)}  "
      f"(bbox {X0:.1f},{Y0:.1f} {W:.1f}x{H:.1f})")
