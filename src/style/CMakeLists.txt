# SPDX-License-Identifier: GPL-3.0-or-later
# klr_style — the *Chlorophyll Intelligence* design system as ONE Qt Quick Controls
# style module (Klorophylle.Style): the design tokens (Theme), the presentation seam
# (Format), the skeleton custom-control overrides, and the generic gap-filler
# components. Everything that touches the theme touches the style, so they share one
# module (see docs/adr/0007). Depends only on klr_core (enums/formatters) + Qt — it is
# the UI foundation klr_gui builds on. Compile-time style selection: screens
# `import Klorophylle.Style`. Every control the app uses is now implemented here; the
# Basic fallback (below) is only a neutral, unbranded safety net for controls we have not
# themed yet and for attached types (see docs/adr/0018).
qt_add_library(klr_style STATIC)

qt_add_qml_module(klr_style
    URI Klorophylle.Style
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Klorophylle/Style"
    # NO qmldir import of QtQuick.Controls or any concrete style. This module IS the active
    # runtime style (main.cpp QQuickStyle::setStyle("Klorophylle.Style")): a qmldir
    # `import QtQuick.Controls` would, while resolving the active style, dispatch right back
    # to this same style — an infinite loop that hangs root construction. And a concrete
    # `import QtQuick.Controls.Basic` would force COMPILE-TIME selection (setStyle becomes a
    # no-op). For runtime selection the fallback for controls we don't implement is wired in
    # code via QQuickStyle::setFallbackStyle("Basic"); screens that use non-styled types
    # (StackView/SplitView/ApplicationWindow, attached ToolTip) import QtQuick.Controls
    # themselves. See docs/adr/0018.
    DEPENDENCIES
        QtQuick.Templates
    QML_FILES
        # Skeleton control overrides (rooted on QtQuick.Templates).
        Button.qml
        ToolButton.qml
        ToolBar.qml
        Label.qml
        MarkdownLabel.qml
        # Skeleton control overrides (rooted on QtQuick.Templates).
        ProgressBar.qml
        # Themed input/navigation/list controls (kill the Material leaks).
        ComboBox.qml
        TextField.qml
        TextArea.qml
        TabBar.qml
        TabButton.qml
        ListItem.qml
        Dialog.qml
        DialogButtonBox.qml
        # Remaining controls themed to drop the Material fallback entirely (docs/adr/0018).
        Switch.qml
        RadioButton.qml
        Menu.qml
        MenuItem.qml
        BusyIndicator.qml
        ToolTip.qml
        ScrollIndicator.qml
        # Generic gap-filler components.
        Card.qml
        SectionHeader.qml
        StatusPill.qml
        PlantCard.qml
        Icon.qml
        # Signature elements.
        PulsingNode.qml
        ReasoningDisclosure.qml
    SOURCES
        tokens.h
        themecontroller.h themecontroller.cpp
        uiformat.h uiformat.cpp
        settingsstore.h settingsstore.cpp
        iconinfo.h
        iconbase.h iconbase.cpp
)

target_link_libraries(klr_style PUBLIC
    Qt6::Quick
    Qt6::QuickControls2
    klr_core
)
target_include_directories(klr_style PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Bundle the brand fonts (SIL OFL — see fonts/*-OFL.txt). One variable TTF per family
# (all weights via the wght axis), registering cleanly as families "Montserrat" /
# "Inter". ThemeController loads them at startup from :/klr/fonts/.
qt_add_resources(klr_style "klr_style_fonts"
    PREFIX "/klr"
    FILES
        fonts/Montserrat-VariableFont.ttf
        fonts/Inter-VariableFont.ttf
        fonts/MaterialSymbolsOutlined.ttf)

# Bundle the brand SVGs (../../assets/branding/, see its README.md) so the window icon
# and the About screen load them at runtime from :/klr/branding/. Aliased so the resource
# path is flat regardless of the on-disk ../../ location. Both the executable and klr_gui
# link klr_style, so these resolve in main.cpp and in QML.
set(_klr_branding "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/branding")
set_source_files_properties("${_klr_branding}/icon.svg"            PROPERTIES QT_RESOURCE_ALIAS icon.svg)
set_source_files_properties("${_klr_branding}/mark.svg"            PROPERTIES QT_RESOURCE_ALIAS mark.svg)
set_source_files_properties("${_klr_branding}/lockup-outlined.svg" PROPERTIES QT_RESOURCE_ALIAS lockup-outlined.svg)
qt_add_resources(klr_style "klr_style_branding"
    PREFIX "/klr/branding"
    FILES
        "${_klr_branding}/icon.svg"
        "${_klr_branding}/mark.svg"
        "${_klr_branding}/lockup-outlined.svg")
