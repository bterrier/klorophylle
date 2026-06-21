// SPDX-License-Identifier: GPL-3.0-or-later
#include "deviceregistry.h"

#include "device_atc.h"
#include "device_bparasite.h"
#include "device_bthome.h"
#include "device_cgdn1.h"
#include "device_esp32_airqualitymonitor.h"
#include "device_esp32_geigercounter.h"
#include "device_esp32_higrow.h"
#include "device_ess_generic.h"
#include "device_flowercare.h"
#include "device_flowerpower.h"
#include "device_hygrotemp_clock.h"
#include "device_hygrotemp_lywsdcgq.h"
#include "device_hygrotemp_square.h"
#include "device_jqjcy01ym.h"
#include "device_parrotpot.h"
#include "device_qingping.h"
#include "device_qingping_cgd1.h"
#include "device_qingping_cgdk2.h"
#include "device_qingping_cgg1.h"
#include "device_qingping_cgp1w.h"
#include "device_ropot.h"
#include "device_thermobeacon.h"
#include "device_wp6003.h"
#include "device_xiaomi.h"

namespace klr {

void DeviceRegistry::add(Matcher matcher, Factory factory)
{
    m_entries.push_back({ std::move(matcher), std::move(factory) });
}

std::unique_ptr<Device> DeviceRegistry::create(const AdvertisementContext &ctx) const
{
    for (const Entry &e : m_entries) {
        if (e.matcher(ctx))
            return e.factory();
    }
    return nullptr;
}

DeviceRegistry makeBuiltinRegistry()
{
    DeviceRegistry r;
    // Specific models first; generic per-format fallbacks last.
    r.add(&DeviceFlowerCare::matches, [] { return std::make_unique<DeviceFlowerCare>(); });
    r.add(&DeviceRoPot::matches, [] { return std::make_unique<DeviceRoPot>(); });
    r.add(&DeviceAtc::matches, [] { return std::make_unique<DeviceAtc>(); });                   // name "ATC*"
    r.add(&DeviceThermoBeacon::matches, [] { return std::make_unique<DeviceThermoBeacon>(); }); // name "ThermoBeacon"
    // Other name-matched models, before the per-format UUID fallbacks below.
    r.add(&DeviceBParasite::matches, [] { return std::make_unique<DeviceBParasite>(); });       // name "bparasite" (BTHome)
    r.add(&DeviceJQJCY01YM::matches, [] { return std::make_unique<DeviceJQJCY01YM>(); });       // name "JQJCY01YM" (MiBeacon)
    r.add(&DeviceHygrotempLYWSDCGQ::matches, [] { return std::make_unique<DeviceHygrotempLYWSDCGQ>(); }); // name "MJ_HT_V1" (MiBeacon)
    r.add(&DeviceCGDN1::matches, [] { return std::make_unique<DeviceCGDN1>(); });               // name "Qingping Air Monitor Lite"
    r.add(&DeviceQingpingCGG1::matches, [] { return std::make_unique<DeviceQingpingCGG1>(); }); // names "ClearGrass Temp & RH" / "Qingping Temp & RH M"
    r.add(&DeviceQingpingCGDK2::matches, [] { return std::make_unique<DeviceQingpingCGDK2>(); }); // name "Qingping Temp RH Lite"
    r.add(&DeviceQingpingCGD1::matches, [] { return std::make_unique<DeviceQingpingCGD1>(); });   // name "Qingping Alarm Clock"
    r.add(&DeviceQingpingCGP1W::matches, [] { return std::make_unique<DeviceQingpingCGP1W>(); }); // name "Qingping Temp RH Barometer"
    // Connection-only devices: no advertisement values, read over GATT (gattProfile).
    r.add(&DeviceFlowerPower::matches, [] { return std::make_unique<DeviceFlowerPower>(); });   // name "Flower power*"
    r.add(&DeviceParrotPot::matches, [] { return std::make_unique<DeviceParrotPot>(); });       // name "Parrot pot*"
    r.add(&DeviceEsp32HiGrow::matches, [] { return std::make_unique<DeviceEsp32HiGrow>(); });   // name "HiGrow"
    r.add(&DeviceEsp32AirQualityMonitor::matches, [] { return std::make_unique<DeviceEsp32AirQualityMonitor>(); }); // name "AirQualityMonitor"
    r.add(&DeviceEsp32GeigerCounter::matches, [] { return std::make_unique<DeviceEsp32GeigerCounter>(); }); // name "GeigerCounter"
    r.add(&DeviceWP6003::matches, [] { return std::make_unique<DeviceWP6003>(); });             // name "6003#*"
    r.add(&DeviceHygrotempSquare::matches, [] { return std::make_unique<DeviceHygrotempSquare>(); }); // LYWSD03MMC/MHO-C401/...
    r.add(&DeviceHygrotempClock::matches, [] { return std::make_unique<DeviceHygrotempClock>(); }); // LYWSD02/MHO-C303
    r.add(&DeviceXiaomi::matches, [] { return std::make_unique<DeviceXiaomi>(); }); // other MiBeacon
    r.add(&DeviceQingping::matches, [] { return std::make_unique<DeviceQingping>(); });
    r.add(&DeviceBtHome::matches, [] { return std::make_unique<DeviceBtHome>(); });
    // Generic standard-service fallback, last: any device advertising ESS 0x181A.
    r.add(&DeviceEssGeneric::matches, [] { return std::make_unique<DeviceEssGeneric>(); });
    return r;
}

} // namespace klr
