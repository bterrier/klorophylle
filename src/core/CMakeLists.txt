# SPDX-License-Identifier: GPL-3.0-or-later
# klr_core — pure, device-agnostic value types + logic. NO decoders, NO Qt Quick /
# Sql / Bluetooth / Charts. (Device/format decoders live in klr_devices.)
qt_add_library(klr_core STATIC
    reading.h
    ids.h
    sensor.h
    binding.h
    binding.cpp
    aggregate.h
    aggregate.cpp
    carestatus.h
    carestatus.cpp
    liveness.h
    liveness.cpp
    bucket.h
    historysync.h
    historysync.cpp
    axis.h
    axis.cpp
    clock.h
    clock.cpp
    format.h
    format.cpp
    units.h
    units.cpp
    ikeyvaluestore.h
    inmemorykeyvaluestore.h
    log.h
    log.cpp
)
target_link_libraries(klr_core PUBLIC Qt6::Core)
target_compile_features(klr_core PUBLIC cxx_std_23)
target_include_directories(klr_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
