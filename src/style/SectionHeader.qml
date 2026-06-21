// SPDX-License-Identifier: GPL-3.0-or-later
// Generic gap-filler: a section title in the Montserrat headline ramp, Dark-Emerald
// (the design system type ramp / "authority"). Extends the style's Label (so it inherits
// the themed Label behaviour) rather than a bare Text. Set `text`.
import QtQuick

Label {
    color: Theme.colorPrimary
    font.family: Theme.fontDisplay
    font.pixelSize: Theme.fontSizeSubtitle
    font.bold: true
    elide: Text.ElideRight
}
