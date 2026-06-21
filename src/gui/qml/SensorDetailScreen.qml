// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Klorophylle.Style
import QtGraphs
import Klorophylle

// A registered sensor's detail page: identity + live status, its per-sensor reading
// history (plant-agnostic — no care band), and a guarded "delete data" action. The sensor
// is chosen by AppContext.selectRegisteredSensor before this is pushed, so the status fields
// (selectedLiveness/Battery/…) and selectedSensorId are already populated. The chart mirrors
// HistoryChartScreen but binds to AppContext.sensorHistory; the same QtGraphs crash note
// applies (gate the GraphsView on a non-degenerate range AND being on a window).
Item {
    id: root
    property string title: AppContext.selectedName

    readonly property string sensorId: AppContext.selectedSensorId
    readonly property var series: AppContext.sensorHistory
    // The quantities this sensor has data for, {value,label} from C++ (labels not in QML).
    readonly property var quantities: AppContext.sensorHistoryQuantities(root.sensorId)
    property int currentQuantity: -1

    readonly property bool plottable: series && !series.empty
                                      && series.tMax > series.tMin
                                      && series.axisMax > series.axisMin

    function loadCurrent() {
        if (root.quantities.length === 0 || quantityBox.currentIndex < 0)
            return;
        root.currentQuantity = root.quantities[quantityBox.currentIndex].value;
        AppContext.loadSensorHistory(root.sensorId, root.currentQuantity);
    }
    Component.onCompleted: loadCurrent()

    // Pop back to the Sensors list once a delete succeeds (the detail target is gone).
    Connections {
        target: AppContext
        function onSensorRemoved(ok, message) {
            if (ok)
                NavigationController.pop();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.marginCompact
        spacing: Theme.spacingBase

        Label {
            text: AppContext.selectedName
            font.pixelSize: Theme.fontSizeTitle
            elide: Label.ElideRight
            Layout.fillWidth: true
        }
        Label {
            text: AppContext.selectedId
            color: Theme.colorTextVariant
            elide: Label.ElideRight
            Layout.fillWidth: true
        }

        // Live status: a green/amber/red dot + battery / last-seen, then a "last synced" line.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                visible: AppContext.selectedLiveness >= 0
                implicitWidth: Theme.spacingSm
                implicitHeight: Theme.spacingSm
                radius: width / 2
                color: Theme.livenessColor(AppContext.selectedLiveness)
            }
            Label {
                Layout.fillWidth: true
                text: {
                    let parts = [];
                    if (AppContext.selectedGattOpen) parts.push(qsTr("connected"));
                    if (AppContext.selectedBatteryText.length > 0)
                        parts.push(qsTr("battery %1").arg(AppContext.selectedBatteryText));
                    if (AppContext.selectedLastSeenText.length > 0)
                        parts.push(qsTr("last seen %1").arg(AppContext.selectedLastSeenText));
                    return parts.join(" · ");
                }
                visible: text.length > 0
                color: AppContext.selectedGattOpen ? Theme.colorAI : Theme.colorTextVariant
                elide: Label.ElideRight
            }
        }
        Label {
            visible: AppContext.selectedLastSyncText.length > 0
            text: qsTr("History last synced %1").arg(AppContext.selectedLastSyncText)
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeCaption
            elide: Label.ElideRight
            Layout.fillWidth: true
        }

        // Pick which quantity's history to plot (only those with stored data).
        RowLayout {
            Layout.fillWidth: true
            visible: root.quantities.length > 0
            spacing: Theme.spacingSm
            Label { text: qsTr("Quantity"); color: Theme.colorTextVariant }
            ComboBox {
                id: quantityBox
                Layout.fillWidth: true
                model: root.quantities
                textRole: "label"
                valueRole: "value"
                onActivated: root.loadCurrent()
            }
        }

        // The history chart (reuses the SeriesModel/QtGraphs path; no ideal-range band).
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Loader {
                id: chartLoader
                anchors.fill: parent
                // Same gate as HistoryChartScreen: build the GraphsView only once the model
                // holds a non-degenerate range AND the page is on a window (avoids the
                // QtGraphs divide-by-zero SIGFPE + "window doesn't exist" bail).
                active: root.plottable && root.Window.window !== null
                sourceComponent: chartComponent
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacingMd
                visible: !root.plottable
                horizontalAlignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.colorTextVariant
                text: root.quantities.length === 0
                      ? qsTr("No readings recorded for this sensor yet.")
                      : qsTr("No history yet for the selected quantity.")
            }
        }

        // Delete the sensor + its data — only when it is not bound to any plant.
        Button {
            Layout.fillWidth: true
            text: qsTr("Delete sensor data")
            enabled: !AppContext.selectedSensorBound
            onClicked: confirmDelete.open()
        }
        Label {
            visible: AppContext.selectedSensorBound
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            // Detaching only closes the binding — the plant still owns this history. The data
            // is deletable only once no plant references the sensor (e.g. its plants are gone).
            text: qsTr("This sensor's readings are part of a plant's history, so they can't be deleted while a plant is using it.")
            color: Theme.colorTextVariant
            font.pixelSize: Theme.fontSizeCaption
        }
    }

    Component {
        id: chartComponent

        GraphsView {
            id: graph
            theme: GraphsTheme {
                colorScheme: Theme.darkActive ? GraphsTheme.ColorScheme.Dark
                                              : GraphsTheme.ColorScheme.Light
                seriesColors: [ Theme.quantityColor(root.currentQuantity) ]
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

            LineSeries {
                id: line
                width: 2
                color: Theme.quantityColor(root.currentQuantity)
            }

            Component.onCompleted: line.replace(root.series.points)
            Connections {
                target: root.series
                function onChanged() { line.replace(root.series.points); }
            }
        }
    }

    Dialog {
        id: confirmDelete
        anchors.centerIn: parent
        // Fixed size + fill-anchored content, like PlantSettingsScreen's delete dialog, to
        // avoid the Material-Dialog implicitHeight binding loop on a word-wrapped Label.
        width: Math.min(parent.width - 2 * Theme.spacingMd, 360)
        height: Math.min(parent.height - 2 * Theme.spacingMd, 220)
        modal: true
        title: qsTr("Delete sensor data?")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: AppContext.removeRegisteredSensor(root.sensorId)

        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("This permanently removes “%1” and all of its stored readings. This cannot be undone.")
                      .arg(AppContext.selectedName)
            }
        }
    }
}
