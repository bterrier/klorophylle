# SPDX-License-Identifier: GPL-3.0-or-later
# klr_ble — passive BLE advertisement scanning. Links Qt6::Bluetooth + klr_devices
# (for the decoders) + klr_core. NO Qt Quick / SQL: the headless probe reuses it.
qt_add_library(klr_ble STATIC
    discovereddevice.h
    blescanner.h
    blescanner.cpp
    gattsession.h
    gattsession.cpp
    gatthistorysession.h
    gatthistorysession.cpp
    gattcommandsession.h
    gattcommandsession.cpp
)
target_link_libraries(klr_ble PUBLIC Qt6::Core Qt6::Bluetooth klr_devices klr_core)
target_include_directories(klr_ble PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
