# SPDX-License-Identifier: GPL-3.0-or-later
# klorophylle — the thin executable: the composition root (main.cpp) only. All UI
# lives in the klr_gui library; the exe links it and its static QML plugin so the
# QML_SINGLETON / type registrations run.
qt_add_executable(klorophylle
    main.cpp
    freedesktopnotificationsink.h freedesktopnotificationsink.cpp
    freedesktopsecretstore.h freedesktopsecretstore.cpp)
target_link_libraries(klorophylle PRIVATE
    klr_gui klr_guiplugin
    klr_style klr_styleplugin
    klr_persistence
    klr_agent      # ISecretStore seam (the Secret Service backend implements it)
    Qt6::DBus  # freedesktop notification + Secret Service backends; DBus confined to the executable
    Qt6::Svg)  # qsvg image-format plugin: lets QIcon (window icon) + QML Image load the brand .svg

# NOTE (docs/adr/0018): runtime style selection resolves QtQuick.Controls + the Basic
# fallback at RUNTIME (so QQuickStyle::setStyle governs the implicit/attached chain). The
# plugin discovery is handled in main.cpp by prepending QLibraryInfo's QML imports dir, so
# no qt_import_qml_plugins()/explicit plugin links are needed here.

# ---- Platform app icons ----------------------------------------------------------------
# All generated from assets/branding/icon.svg by assets/generate-icons.sh (mirrors the legacy
# WatchFlower assets/ layout). The in-app window icon is set in main.cpp from the bundled
# :/klr/branding/icon.svg; the per-platform packaging icons below are wired for DESKTOP now.
# Android/iOS assets are generated and STAGED — the package-source hooks point at them so the
# mobile build picks them up, but they are harmless no-ops on a desktop build.
set(_klr_assets "${CMAKE_CURRENT_SOURCE_DIR}/../../assets")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(klorophylle PRIVATE "${_klr_assets}/windows/klorophylle.rc")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_klr_icns "${_klr_assets}/macos/klorophylle.icns")
    set_source_files_properties("${_klr_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(klorophylle PRIVATE "${_klr_icns}")
    set_target_properties(klorophylle PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${_klr_assets}/macos/Info.plist"
        MACOSX_BUNDLE_ICON_FILE "klorophylle.icns")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(GNUInstallDirs)
    install(FILES "${_klr_assets}/linux/klorophylle.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
    install(FILES "${_klr_assets}/linux/klorophylle.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps")
    foreach(_sz 16 24 32 48 64 128 256 512)
        install(FILES "${_klr_assets}/linux/hicolor/${_sz}x${_sz}/apps/klorophylle.png"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_sz}x${_sz}/apps")
    endforeach()
endif()

# Mobile packaging — staged: point the package sources at the generated assets.
if(ANDROID)
    set_target_properties(klorophylle PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${_klr_assets}/android")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_klr_xcassets "${_klr_assets}/ios/Images.xcassets")
    set_source_files_properties("${_klr_xcassets}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(klorophylle PRIVATE "${_klr_xcassets}")
    set_target_properties(klorophylle PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${_klr_assets}/ios/Info.plist"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
endif()
