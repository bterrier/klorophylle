# SPDX-License-Identifier: GPL-3.0-or-later
# klr_gui — the UI as a LIBRARY (so it is unit/QML testable on its own): the single
# QML module "Klorophylle" plus its view-models. The thin executable lives in src/app.
qt_add_library(klr_gui STATIC)

qt_add_qml_module(klr_gui
    URI Klorophylle
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Klorophylle"  # end in the module path (tooling)
    # The screens select our compile-time style + tokens by importing Klorophylle.Style.
    # DEPENDENCIES tells qmllint/qmlcachegen about it; IMPORT_PATH points at the build
    # qml/ root where its qmldir lives (../style writes qml/Klorophylle/Style).
    DEPENDENCIES
        Klorophylle.Style
    IMPORT_PATH "${CMAKE_BINARY_DIR}/qml"
    QML_FILES
        qml/Main.qml
        qml/ScanScreen.qml
        qml/LiveScreen.qml
        qml/SensorDetailScreen.qml
        qml/PlantsScreen.qml
        qml/PlantDetailScreen.qml
        qml/PlantSettingsScreen.qml
        qml/HistoryChartScreen.qml
        qml/SpeciesPickerDialog.qml
        qml/DuplicateDialog.qml
        qml/NavRail.qml
        qml/SettingsScreen.qml
        qml/AboutScreen.qml
        qml/ExportScreen.qml
        qml/AIInsightsScreen.qml
        qml/GlobalJournalScreen.qml
    SOURCES
        appcontext.h appcontext.cpp
        navigationcontroller.h navigationcontroller.cpp
        discovereddevicesmodel.h discovereddevicesmodel.cpp
        devicesortfiltermodel.h devicesortfiltermodel.cpp
        livereadingsmodel.h livereadingsmodel.cpp
        plantlistmodel.h plantlistmodel.cpp
        careevaluation.h careevaluation.cpp
        plantjournalmodel.h plantjournalmodel.cpp
        plantcaremodel.h plantcaremodel.cpp
        sensorstatusmodel.h sensorstatusmodel.cpp
        registeredsensorsmodel.h registeredsensorsmodel.cpp
        readingingester.h readingingester.cpp
        inotificationsink.h
        alertcontroller.h alertcontroller.cpp
        historysynccontroller.h historysynccontroller.cpp
        carethresholdsmodel.h carethresholdsmodel.cpp
        catalogsearchmodel.h catalogsearchmodel.cpp
        seriesmodel.h seriesmodel.cpp
        journalformat.h journalformat.cpp
        buildinfo.h buildinfo.cpp
        agentviewmodel.h agentviewmodel.cpp
)

target_link_libraries(klr_gui PUBLIC
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Graphs
    klr_ble
    klr_core
    klr_persistence
    klr_style
    klr_version
    klr_agent
)
target_include_directories(klr_gui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
