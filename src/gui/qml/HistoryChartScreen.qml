// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Klorophylle.Style
import QtGraphs
import Klorophylle

// One quantity's history for the selected plant, plotted with QtGraphs. The whole
// series + axis ranges come from the C++ SeriesModel (AppContext.history): QtGraphs does
// not auto-range, so the ValueAxis/DateTimeAxis bind to the model's pre-computed bounds,
// and QML replaces the model's points into its OWN LineSeries — C++ never touches a QML
// series. See docs/adr/0006-history-charts-import.md.
//
// CRASH NOTE: QGraphsView divides by the axis span during updatePolish(), so it SIGFPEs
// if instantiated with a degenerate range. Because the page is created on StackView.push
// BEFORE loadHistory() fills the model, we must NOT build the GraphsView until the model
// holds real, non-degenerate data — hence the Loader gate below.
Item {
    id: root

    // Set by the pusher (the care list row that was tapped).
    required property int quantity
    required property string quantityLabel
    property string title: qsTr("%1 history").arg(quantityLabel)

    // var-typed alias so QML resolves the SeriesModel's properties dynamically.
    readonly property var series: AppContext.history

    // True only once the model has points AND both axes have a non-zero span — the
    // precondition for QtGraphs not to divide by zero.
    readonly property bool plottable: series && !series.empty
                                      && series.tMax > series.tMin
                                      && series.axisMax > series.axisMin

    // Fill the model first; the Loader then activates once `plottable` flips true.
    Component.onCompleted: AppContext.loadHistory(root.quantity)

    Loader {
        id: chartLoader
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        // Build the GraphsView only once the page is BOTH populated (non-degenerate range —
        // the crash note above) AND attached to a window. QtGraphs' AxisRenderer::initialize()
        // runs off a QTimer(0) from updatePolish and logs "qt.graphs2d.critical: window doesn't
        // exist." (then bails) if it fires while the item has no QQuickWindow — which happens
        // here because the StackView page is created, and loadHistory fills the model, BEFORE
        // the page is shown. Gating on Window.window defers creation until it is on a window.
        active: root.plottable && root.Window.window !== null
        sourceComponent: chartComponent
    }

    Component {
        id: chartComponent

        GraphsView {
            id: graph

            // QtGraphs needs an explicit theme (else it logs "Theme not found" and uses
            // its built-in green palette, which collides with our green ideal band). Map
            // it onto our design tokens + the reading-type line colour.
            theme: GraphsTheme {
                colorScheme: Theme.darkActive ? GraphsTheme.ColorScheme.Dark
                                               : GraphsTheme.ColorScheme.Light
                seriesColors: [ Theme.quantityColor(root.quantity) ]
                backgroundVisible: false
                plotAreaBackgroundVisible: false
                grid.mainColor: Theme.colorOutline
                grid.subColor: Theme.colorOutline
                axisX.mainColor: Theme.colorOutline
                axisX.labelTextColor: Theme.colorTextVariant
                axisY.mainColor: Theme.colorOutline
                axisY.labelTextColor: Theme.colorTextVariant
                labelTextColor: Theme.colorTextVariant
            }

            axisX: DateTimeAxis {
                min: root.series.tMinDate
                max: root.series.tMaxDate
                // Tick COUNT + label format from the model (niceTimeAxis). A DateTimeAxis's
                // tickInterval is a division count clamped to [0,100] — NOT a ms spacing;
                // the old span/5 clamped to 100 and flooded the axis with gridlines.
                labelFormat: root.series.xLabelFormat
                tickInterval: root.series.xTickInterval
                subTickCount: 0
            }
            axisY: ValueAxis {
                min: root.series.axisMin
                max: root.series.axisMax
                tickInterval: root.series.tickInterval
                subTickCount: 0
            }

            // The ideal-range band: a soft green zone between the plant's min/max
            // for this quantity (already in the chart's display unit). Drawn first so the
            // line sits on top; hidden when the plant has no threshold for the quantity.
            AreaSeries {
                id: band
                visible: root.series.hasBand
                color: Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.12)
                borderColor: Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.35)
                borderWidth: 1
                upperSeries: LineSeries { id: bandTop }
                lowerSeries: LineSeries { id: bandBottom }
            }

            LineSeries {
                id: line
                width: 2
                // Reading-type colour (blue=water, amber=light, red=temperature, …) so the
                // line stays legible over the green ideal band. See ThemeController.
                color: Theme.quantityColor(root.quantity)
            }

            // Rebuild the flat band edges across the visible time span from the model's
            // scalar bounds (trivial view glue — the values come from C++).
            function rebuildBand() {
                bandTop.clear();
                bandBottom.clear();
                if (!root.series.hasBand)
                    return;
                bandTop.append(root.series.tMin, root.series.bandMax);
                bandTop.append(root.series.tMax, root.series.bandMax);
                bandBottom.append(root.series.tMin, root.series.bandMin);
                bandBottom.append(root.series.tMax, root.series.bandMin);
            }

            // The model is already populated when this loads; keep it in sync if more
            // readings arrive while the chart is open.
            Component.onCompleted: { line.replace(root.series.points); rebuildBand(); }
            Connections {
                target: root.series
                function onChanged() { line.replace(root.series.points); graph.rebuildBand(); }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spacingMd
        visible: !root.plottable
        horizontalAlignment: Qt.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.colorTextVariant
        text: qsTr("No history yet for %1.\nReadings appear here once the plant's sensor has reported a few values.").arg(root.quantityLabel)
    }
}
