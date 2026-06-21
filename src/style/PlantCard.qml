// SPDX-License-Identifier: GPL-3.0-or-later
// The rich plant card for the Plants home (the design system "Cards" + the
// Stitch "My Plants" spec). Presentational ONLY — it takes plain input properties and
// holds no model/AppContext reference, so it stays in the domain-agnostic style module
// and is reused by any list that maps PlantListModel's roles onto it. The screen wraps
// it in the clickable delegate; this card draws.
//
// Compact layout: a leaf-glyph photo slot + name/pill/chevron on the top line, the
// scientific name below, and the moisture/light metric bars in a row beneath. A plant
// with no sensor (no current readings) stays first-class — the metric row hides and the
// name + pill remain.
import QtQuick
import QtQuick.Layouts

Card {
    id: root

    // Inputs — mapped from PlantListModel roles by the screen.
    property string displayName
    property string species
    property int health: 0 // CareLevel: 0 Unknown, 1 Good, 2 Attention
    property int connectivity: -1 // Liveness: 0 Offline, 1 Stale, 2 Live; <0 = no sensor (no dot)
    property var moisture: ({}) // { present, valueText, fraction, hasRange }
    property var light: ({})

    // Emitted on tap. The card IS the tap surface (a Pane consumes mouse events, so a
    // wrapping ItemDelegate would never see the click) — the screen connects this to nav.
    signal clicked()

    padding: Theme.spacingSm

    // Collapse the status pill to its compact disk when the top row can't show everything
    // at natural size (e.g. the master pane narrows when the detail opens). Computed from
    // intrinsic widths only — none depend on the pill's mode, so the binding can't oscillate.
    readonly property real topRowFullWidth:
        photo.implicitWidth + topRow.spacing + nameCol.implicitWidth
        + (livenessDot.visible ? livenessDot.implicitWidth + topRow.spacing : 0)
        + (statusPill.visible ? statusPill.fullWidth + topRow.spacing : 0)
        + chevron.implicitWidth + topRow.spacing
    readonly property bool pillCompact: topRow.width > 0 && topRow.width < topRowFullWidth

    TapHandler {
        onTapped: root.clicked()
    }
    // Clickability cue: pointing-hand cursor + an emerald border on hover (a true ambient
    // shadow/elevation is Slice C).
    HoverHandler {
        id: hoverHandler
        cursorShape: Qt.PointingHandCursor
    }
    background: Rectangle {
        color: Theme.colorCard
        radius: Theme.radius
        border.width: 1
        border.color: hoverHandler.hovered ? Theme.colorGood : Theme.colorCardBorder
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }

    // One metric line: icon + (gradient bar when an ideal range exists) + value text.
    // Hidden entirely when the plant has no current reading for the quantity.
    component MetricLine: RowLayout {
        id: line
        property string glyph
        property var metric: ({})
        visible: metric.present === true
        spacing: Theme.spacingXs
        Icon {
            icon.name: line.glyph
            icon.size: Theme.fontSizeBody
            icon.color: Theme.colorTextVariant
        }
        ProgressBar {
            visible: line.metric.hasRange === true
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            value: line.metric.fraction ?? 0
        }
        Item { visible: !(line.metric.hasRange === true); Layout.fillWidth: true }
        Label {
            text: line.metric.valueText ?? ""
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeCaption
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs

        RowLayout {
            id: topRow
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            // Photo slot — leaf glyph placeholder until plant/journal photos land.
            Rectangle {
                id: photo
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Theme.spacingLg
                implicitHeight: Theme.spacingLg
                radius: Theme.radiusMd
                color: Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.12)
                Icon {
                    anchors.centerIn: parent
                    icon.name: "potted_plant"
                    icon.size: Theme.fontSizeTitle
                    icon.color: Theme.colorGood
                }
            }

            ColumnLayout {
                id: nameCol
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: root.displayName
                    font.bold: true
                    elide: Label.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: root.species.length > 0
                    text: root.species
                    color: Theme.colorTextVariant
                    font.pixelSize: Theme.fontSizeCaption
                    elide: Label.ElideRight
                    Layout.fillWidth: true
                }
            }

            // Live connectivity of the plant's sensor(s): green broadcasting & fresh,
            // amber broadcasting but stale, red not heard for >60s. Hidden for a plant with
            // no bound sensor (connectivity < 0) — it is not "offline", it just has no sensor.
            Rectangle {
                id: livenessDot
                Layout.alignment: Qt.AlignVCenter
                visible: root.connectivity >= 0
                implicitWidth: Theme.spacingSm
                implicitHeight: Theme.spacingSm
                radius: width / 2
                color: Theme.livenessColor(root.connectivity)
            }

            // At-a-glance care status; hidden when nothing is judged yet. Collapses
            // to an icon disk when the row is cramped, yielding width to the name.
            StatusPill {
                id: statusPill
                Layout.alignment: Qt.AlignVCenter
                visible: root.health > 0
                compact: root.pillCompact
                text: Format.careLevelLabel(root.health)
                iconName: Format.careLevelIcon(root.health)
                pillColor: Theme.careLevelColor(root.health)
            }
            Icon {
                id: chevron
                Layout.alignment: Qt.AlignVCenter
                icon.name: "chevron_right"
                icon.size: Theme.fontSizeTitle
                opacity: 0.4
            }
        }

        // Moisture + light, side by side. Hidden as a block for a sensorless plant.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingXs
            spacing: Theme.spacingMd
            visible: moistureLine.visible || lightLine.visible
            MetricLine {
                id: moistureLine
                Layout.fillWidth: true
                glyph: "water_drop"
                metric: root.moisture
            }
            MetricLine {
                id: lightLine
                Layout.fillWidth: true
                glyph: "light_mode"
                metric: root.light
            }
        }
    }
}
