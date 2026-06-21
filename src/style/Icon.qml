// SPDX-License-Identifier: GPL-3.0-or-later
// The one icon primitive used across the app. Set `icon.name` for a Material Symbols
// glyph (a ligature, e.g. "delete"/"water_drop") or `icon.source` for an image (e.g. an
// SVG); name takes precedence. `icon.color`/`icon.size` are optional overrides.
//
// The Material Symbols font MUST be drawn with Text.NativeRendering — the default
// distance-field renderer mangles large/variable icon glyphs. Derives from the C++
// IconBase (which owns the typed `icon` grouped property; Qt's QQuickIcon is private).
import QtQuick

IconBase {
    id: root

    // Effective pixel size: the icon.size override if given, else a body-ish default.
    readonly property real effectiveSize: root.icon.size > 0 ? root.icon.size : Theme.fontSizeTitle

    implicitWidth: effectiveSize
    implicitHeight: effectiveSize

    // Glyph branch — Material Symbols ligature.
    Text {
        anchors.centerIn: parent
        visible: root.icon.name.length > 0 && root.icon.source.toString().length === 0
        text: root.icon.name
        font.family: Theme.fontIcon
        font.pixelSize: root.effectiveSize
        color: root.icon.color.a > 0 ? root.icon.color : Theme.colorText
        renderType: Text.NativeRendering // icon fonts require the native renderer
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    // Image branch — any source URL (SVG/PNG). Rendered as-is (no tinting).
    Image {
        anchors.centerIn: parent
        visible: root.icon.source.toString().length > 0
        source: root.icon.source
        sourceSize.width: root.effectiveSize
        sourceSize.height: root.effectiveSize
        fillMode: Image.PreserveAspectFit
    }
}
