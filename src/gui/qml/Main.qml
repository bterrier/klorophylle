// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls // ApplicationWindow/SplitView/StackView — not in Klorophylle.Style, runtime-styled
import Klorophylle.Style
import QtQuick.Layouts
import Klorophylle

// Responsive shell: a dark-emerald NavRail (Plants / Sensors / More) beside a master/
// detail SplitView. The MASTER pane is the current section's home (the plant list or the
// sensor scan), driven by NavigationController.currentSection; the DETAIL pane is a
// StackView mirroring the pages pushed on top of it (plant detail, history, settings…).
// On wide form factors both panes show side by side; on a phone the layout collapses to
// one pane at a time — the master until something is selected, then the detail. The same
// NavigationController stack drives both: its root is always a section, so the detail is
// simply "everything above the root" and canGoBack tells us whether it is non-empty.
// The view binds the injected AppContext/NavigationController singletons — no setContextProperty.
ApplicationWindow {
    id: window
    width: 1180
    height: 760
    minimumWidth: 940
    minimumHeight: 600
    visible: true
    title: qsTr("Klorophylle")
    color: Theme.colorBackground

    // The persisted colour-scheme choice (SettingsStore) drives the live Theme — applied
    // at startup and whenever the user changes it in Settings. One-way view-glue so
    // ThemeController stays free of persistence (docs/adr/0008).
    Binding {
        target: Theme
        property: "colorScheme"
        value: Settings.colorScheme
    }

    // Window width drives the form factor (breakpoints unit-tested in ThemeController), so
    // the whole tree adapts off the one C++ input Theme.formFactor.
    Binding {
        target: Theme
        property: "formFactor"
        value: Theme.formFactorForWidth(window.width)
    }

    // Phone = one pane at a time; tablet/desktop = master + detail side by side.
    readonly property bool narrow: Theme.formFactor === Theme.Phone

    // Route -> the Component to instantiate. The map MUST live in QML (Components are QML
    // objects); NavigationController owns the logical route stack, this view mirrors it.
    function routeComponent(route) {
        switch (route) {
        case NavigationController.Plants: return plantsComponent;
        case NavigationController.PlantDetail: return plantDetailComponent;
        case NavigationController.PlantSettings: return plantSettingsComponent;
        case NavigationController.History: return historyChartComponent;
        case NavigationController.Sensors: return sensorsComponent;
        case NavigationController.Live: return liveComponent;
        case NavigationController.SensorDetail: return sensorDetailComponent;
        case NavigationController.Settings: return settingsComponent;
        case NavigationController.SettingsCategory: return settingsCategoryComponent;
        case NavigationController.Export: return exportComponent;
        case NavigationController.About: return aboutComponent;
        case NavigationController.AIInsights: return aiInsightsComponent;
        case NavigationController.GlobalJournal: return globalJournalComponent;
        }
        return plantsComponent;
    }

    // Mirror the pushed pages onto the detail StackView (the only navigation glue). The
    // master Loader follows currentSection on its own, so a section reset just empties the
    // detail back to the placeholder.
    Connections {
        target: NavigationController
        function onPushed(route, args) { stack.push(window.routeComponent(route), args); }
        function onPopped() { stack.pop(); }
        function onSectionReset(route) { stack.clear(); }
        // Selecting a master item swaps the detail for a single page (no A->B stacking).
        function onReplaced(route, args) { stack.clear(); stack.push(window.routeComponent(route), args); }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavRail { Layout.fillHeight: true }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            // ---- MASTER: the current section's home (plant list / sensor scan) ----
            ColumnLayout {
                id: masterPane
                spacing: 0
                SplitView.preferredWidth: 320
                SplitView.minimumWidth: 260
                // Hidden for app-level full-page routes (Settings/Export/About) — the list
                // beside Settings makes no sense. Otherwise wide: always shown; narrow:
                // only while nothing is selected.
                visible: !NavigationController.currentIsFullPage
                         && (!window.narrow || !NavigationController.canGoBack)

                ToolBar {
                    Layout.fillWidth: true
                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingBase
                        verticalAlignment: Label.AlignVCenter
                        font.family: Theme.fontDisplay
                        font.bold: true
                        font.pixelSize: Theme.fontSizeSubtitle
                        elide: Label.ElideRight
                        text: master.item?.title ?? window.title // qmllint disable missing-property
                    }
                }

                Loader {
                    id: master
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sourceComponent: NavigationController.currentSection === NavigationController.Sensors
                                     ? sensorsComponent : plantsComponent
                }
            }

            // ---- DETAIL: the pages pushed over the section home ----
            ColumnLayout {
                id: detailPane
                spacing: 0
                SplitView.fillWidth: true
                // Shown only once something is pushed; when the detail is empty the master
                // pane is the sole visible child and fills the whole content area (no empty
                // placeholder).
                visible: NavigationController.canGoBack

                ToolBar {
                    Layout.fillWidth: true
                    RowLayout {
                        anchors.fill: parent
                        ToolButton {
                            visible: NavigationController.canGoBack
                            onClicked: NavigationController.pop()
                            icon.name: "arrow_back"
                        }
                        Label {
                            Layout.fillWidth: true
                            font.family: Theme.fontDisplay
                            font.bold: true
                            font.pixelSize: Theme.fontSizeSubtitle
                            elide: Label.ElideRight
                            text: stack.currentItem?.title ?? "" // qmllint disable missing-property
                        }
                    }
                }

                StackView {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true // contain push/pop slide transitions within the pane
                }
            }
        }
    }

    // The screens drive navigation themselves through NavigationController, so the shell
    // just supplies the Component for each route. plantsComponent/sensorsComponent back the
    // master Loader; the rest are pushed onto the detail StackView.
    Component { id: plantsComponent; PlantsScreen {} }
    Component { id: plantDetailComponent; PlantDetailScreen {} }
    Component { id: plantSettingsComponent; PlantSettingsScreen {} }
    Component { id: historyChartComponent; HistoryChartScreen {} }
    Component { id: sensorsComponent; ScanScreen {} }
    Component { id: liveComponent; LiveScreen {} }
    Component { id: sensorDetailComponent; SensorDetailScreen {} }
    Component { id: settingsComponent; SettingsScreen {} }
    Component { id: settingsCategoryComponent; SettingsCategoryScreen {} }
    Component { id: exportComponent; ExportScreen {} }
    Component { id: aboutComponent; AboutScreen {} }
    Component { id: aiInsightsComponent; AIInsightsScreen {} }
    Component { id: globalJournalComponent; GlobalJournalScreen {} }
}
