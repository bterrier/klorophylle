# SPDX-License-Identifier: GPL-3.0-or-later
# klr_devices — the device layer. One Device subclass per supported device
# (device_*.h, each self-contained), a registry that instantiates the matching
# one, and the pure wire-format decoders (AdvertisementParser, ported from
# WatchFlower) the device classes delegate to. Pure: depends on klr_core only.
qt_add_library(klr_devices STATIC
    advertisementdata.h
    advertisementparser.h
    advertisementparser.cpp
    mibeacon_auth.h
    mibeacon_auth.cpp
    device.h
    device_flowercare.h
    device_ropot.h
    device_xiaomi.h
    device_qingping.h
    device_bthome.h
    device_atc.h
    device_thermobeacon.h
    device_bparasite.h
    device_jqjcy01ym.h
    device_hygrotemp_lywsdcgq.h
    device_cgdn1.h
    device_qingping_cgg1.h
    device_qingping_cgdk2.h
    device_qingping_cgd1.h
    device_qingping_cgp1w.h
    gattprofile.h
    gatthistoryprofile.h
    device_flowerpower.h
    device_parrotpot.h
    device_esp32_higrow.h
    device_esp32_airqualitymonitor.h
    device_esp32_geigercounter.h
    device_wp6003.h
    device_hygrotemp_square.h
    device_hygrotemp_clock.h
    device_ess_generic.h
    deviceregistry.h
    deviceregistry.cpp
)
target_link_libraries(klr_devices PUBLIC Qt6::Core klr_core)
target_include_directories(klr_devices PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
