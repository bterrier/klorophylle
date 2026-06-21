// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of Dialog. Rooted on the
// QtQuick.Templates type (NEVER QtQuick.Controls). Replaces the raw Material chrome on
// the app's ~12 dialogs: a card-surface background with the 1px outline rule and a
// rounded corner, a header showing the title in the Montserrat authority ramp, and a
// footer that resolves to our themed DialogButtonBox (so the standard buttons are our
// Buttons). The modal dim is a plain emerald-tinted scrim for now — the formal
// backdrop-scrim token and the ~12px "glass greenhouse" blur are Slice C.
import QtQuick
import QtQuick.Templates as T
import QtQuick.Effects

T.Dialog {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth, implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    padding: Theme.spacingMd

    background: Item {
        // Soft ambient elevation (the design system) — a dialog floats above the scrim.
        RectangularShadow {
            anchors.fill: surface
            radius: surface.radius
            blur: Theme.elevationBlur
            offset.y: Theme.elevationOffsetY
            color: Theme.colorShadow
        }
        Rectangle {
            id: surface
            anchors.fill: parent
            radius: Theme.radiusMd
            color: Theme.colorCard
            border.width: 1
            border.color: Theme.colorCardBorder
        }
    }

    header: Item {
        visible: control.title.length > 0
        implicitHeight: control.title.length > 0 ? titleLabel.implicitHeight + 2 * Theme.spacingMd : 0
        Text {
            id: titleLabel
            anchors.fill: parent
            anchors.margins: Theme.spacingMd
            anchors.bottomMargin: Theme.spacingBase
            text: control.title
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontSizeSubtitle
            font.bold: true
            color: Theme.colorPrimary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    footer: DialogButtonBox {
        visible: count > 0
    }

    // Modal backdrop scrim — the formal emerald-tinted dim token. A true
    // ~12px backdrop blur ("glass greenhouse") is deferred: QML can't sample the item
    // behind the overlay without the front Item knowing it, so it stays a flagged
    // follow-up (see ADR 0013 / the design system).
    T.Overlay.modal: Rectangle {
        color: Theme.colorBackdropScrim
    }
    T.Overlay.modeless: Rectangle {
        color: Theme.colorBackdropScrim
    }
}
