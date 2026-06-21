// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from WatchFlower's src/device_sensor_advertisement.cpp
// (Copyright (c) 2022 Emeric Grange, GPL-3.0-or-later). Decode logic is kept
// faithful; only the `adv_mode` parameter, the DeviceSensor::parseBeacon* glue,
// and the DeviceUtils dependency were removed.
#include "advertisementparser.h"

#include <cmath>
#include <cstdint>

namespace klr {

namespace {

enum MiBeacon_sensors {
    mi_temperature = 0x1004,
    mi_humidity = 0x1006,
    mi_luminosity = 0x1007,
    mi_soil_moisture = 0x1008,
    mi_soil_conductivity = 0x1009,
    mi_battery_level = 0x100a,
    mi_temperature_humidity = 0x100d,
    mi_formaldehyde = 0x1010,
};

enum Qingping_sensors {
    qp_temperature_humidity = 0x01,
    qp_battery_level = 0x02,
    qp_air_pressure = 0x07,
    qp_particulate_matter = 0x12,
    qp_co2 = 0x13,
};

enum BtHome_sensors {
    bth_packetid_uint8 = 0x00,
    bth_battery_uint8 = 0x01,
    bth_co2_uint16 = 0x12,
    bth_humidity_uint16 = 0x03,
    bth_humidity_uint8 = 0x2E,
    bth_illuminance_uint24 = 0x05,
    bth_moisture_uint16 = 0x14,
    bth_moisture_uint8 = 0x2F,
    bth_pm25_uint16 = 0x0D,
    bth_pm10_uint16 = 0x0E,
    bth_pressure_uint24 = 0x04,
    bth_temperature_sint16 = 0x45,
    bth_temperature_p_sint16 = 0x02,
    bth_tvoc_uint16 = 0x13,
    bth_voltage_p_uint16 = 0x0C,
    bth_binary_power = 0x10,
};

} // namespace

SensorAdvData AdvertisementParser::decodeXiaomi(uint16_t adv_id, const QByteArray &ba)
{
    SensorAdvData d;

    const quint8 *data = reinterpret_cast<const quint8 *>(ba.constData());
    const int data_size = ba.size();

    if (adv_id == 0xFE95 && data_size >= 12) {
        uint16_t framecontrol = static_cast<uint16_t>(data[0] + (data[1] << 8));
        bool hasCapability = (framecontrol & 0x0020);
        bool hasObject = (framecontrol & 0x0040);

        int pos = 11;

        if (hasCapability) {
            int capability = static_cast<uint8_t>(data[pos++]);
            if (capability & 0x20) // IO capability field
                pos += 2;
        }

        if (hasObject) {
            int payload_data_type = static_cast<int>(data[pos] + (data[pos + 1] << 8));
            pos += 3; // type (2) + size (1)

            if (payload_data_type == mi_battery_level) {
                d.battery = static_cast<int>(data[pos++]);
            } else if (payload_data_type == mi_soil_moisture) {
                int moist = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (moist >= 0 && moist <= 100)
                    d.soilMoisture = moist;
            } else if (payload_data_type == mi_soil_conductivity) {
                int fert = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (fert >= 0 && fert < 20000)
                    d.soilConductivity = fert;
            } else if (payload_data_type == mi_temperature_humidity) {
                float temp = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (temp > -30.f && temp < 100.f)
                    d.temperature = temp;
                float humi = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (humi >= 0.f && humi <= 100.f)
                    d.humidity = humi;
            } else if (payload_data_type == mi_temperature) {
                float temp = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (temp > -30.f && temp < 100.f)
                    d.temperature = temp;
            } else if (payload_data_type == mi_humidity) {
                float humi = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (humi >= 0.f && humi <= 100.f)
                    d.humidity = humi;
            } else if (payload_data_type == mi_luminosity) {
                int lumi = static_cast<int32_t>(data[pos] + (data[pos + 1] << 8) + (data[pos + 2] << 16));
                pos += 3;
                if (lumi >= 0 && lumi < 150000)
                    d.luminosity = lumi;
            } else if (payload_data_type == mi_formaldehyde) {
                float hcho = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (hcho >= 0.f && hcho <= 100.f)
                    d.hcho = hcho;
            }
            Q_UNUSED(pos)
        }
    }

    return d;
}

SensorAdvData AdvertisementParser::decodeQingping(uint16_t adv_id, const QByteArray &ba)
{
    SensorAdvData d;

    const quint8 *data = reinterpret_cast<const quint8 *>(ba.constData());
    const int data_size = ba.size();

    if (adv_id == 0xFDCD && data_size >= 14) {
        for (int pos = 8; pos < data_size;) {
            int payload_data_type = data[pos++];
            int payload_data_size = data[pos++];
            Q_UNUSED(payload_data_size)

            if (payload_data_type == qp_battery_level) {
                d.battery = static_cast<int>(data[pos]);
            } else if (payload_data_type == qp_temperature_humidity) {
                float temp = static_cast<int32_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (temp > -30.f && temp < 100.f)
                    d.temperature = temp;
                float humi = static_cast<int32_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (humi >= 0.f && humi <= 100.f)
                    d.humidity = humi;
            } else if (payload_data_type == qp_air_pressure) {
                float pres = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (pres >= 0 && pres <= 2000)
                    d.pressure = pres;
            } else if (payload_data_type == qp_particulate_matter) {
                float pm25 = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (pm25 >= 0 && pm25 <= 1000)
                    d.pm25 = pm25;
                float pm10 = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (pm10 >= 0 && pm10 <= 1000)
                    d.pm10 = pm10;
            } else if (payload_data_type == qp_co2) {
                float co2 = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (co2 >= 0 && co2 <= 9999)
                    d.co2 = co2;
            } else {
                // unknown payload type: position left as-is (matches WatchFlower's behaviour)
            }
        }
    }

    return d;
}

SensorAdvData AdvertisementParser::decodeBtHome(uint16_t adv_id, const QByteArray &ba)
{
    SensorAdvData d;

    const quint8 *data = reinterpret_cast<const quint8 *>(ba.constData());
    const int data_size = ba.size();

    bool isEncrypted = false;
    int version = 0;
    int pos = 0;

    if (adv_id == 0x181E) {
        version = 1; // BTHome v1, encrypted
        isEncrypted = true;
    } else if (adv_id == 0x181C) {
        version = 1; // BTHome v1
        isEncrypted = false;
    } else if (adv_id == 0xFCD2 && data_size >= 1) {
        uint8_t bthome_info_byte = data[pos++]; // BTHome v2
        isEncrypted = (bthome_info_byte & 0x0001);
        version = (bthome_info_byte & 0xE0) >> 5;
    }

    if (isEncrypted) {
        // BTHome encrypted payloads are unsupported.
    } else if (version == 1 || version == 2) {
        for (; pos < data_size;) {
            uint8_t object_type = 0;
            int object_length = 0;

            if (version == 1) {
                uint8_t bthome_object = data[pos++];
                object_length = (bthome_object & 0x1F);
            }

            object_type = data[pos++];

            if (object_type == bth_packetid_uint8) {
                pos++;
            } else if (object_type == bth_battery_uint8) {
                d.battery = static_cast<int8_t>(data[pos++]);
            } else if (object_type == bth_temperature_sint16) {
                float temp = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8)) / 10.f;
                pos += 2;
                if (temp > -30.f && temp < 100.f)
                    d.temperature = temp;
            } else if (object_type == bth_temperature_p_sint16) {
                float temp = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8)) / 100.f;
                pos += 2;
                if (temp > -30.f && temp < 100.f)
                    d.temperature = temp;
            } else if (object_type == bth_humidity_uint8) {
                float humi = static_cast<float>(data[pos++]);
                if (humi >= 0.f && humi <= 100.f)
                    d.humidity = humi;
            } else if (object_type == bth_humidity_uint16) {
                float humi = static_cast<uint16_t>(data[pos] + (data[pos + 1] << 8)) / 100.f;
                pos += 2;
                if (humi >= 0.f && humi <= 100.f)
                    d.humidity = humi;
            } else if (object_type == bth_illuminance_uint24) {
                int lumi = static_cast<int>(
                    static_cast<uint32_t>(data[pos] + (data[pos + 1] << 8) + (data[pos + 2] << 16)) / 100.f);
                pos += 3;
                if (lumi >= 0 && lumi < 150000)
                    d.luminosity = lumi;
            } else if (object_type == bth_moisture_uint16) {
                int moist = static_cast<int>(
                    static_cast<uint16_t>(data[pos] + (data[pos + 1] << 8)) / 100.f);
                pos += 2;
                if (moist >= 0 && moist <= 100)
                    d.soilMoisture = moist;
            } else if (object_type == bth_moisture_uint8) {
                int moist = static_cast<uint8_t>(data[pos++]);
                if (moist >= 0 && moist <= 100)
                    d.soilMoisture = moist;
            } else if (object_type == bth_pressure_uint24) {
                float pres = static_cast<uint32_t>(data[pos] + (data[pos + 1] << 8) + (data[pos + 2] << 16)) / 100.f;
                pos += 3;
                if (pres >= 0 && pres <= 2000)
                    d.pressure = pres;
            } else if (object_type == bth_tvoc_uint16) {
                float voc = static_cast<uint16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (voc >= 0 && voc <= 9999)
                    d.voc = voc;
            } else if (object_type == bth_co2_uint16) {
                float co2 = static_cast<int16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (co2 >= 0 && co2 <= 9999)
                    d.co2 = co2;
            } else if (object_type == bth_pm25_uint16) {
                float pm25 = static_cast<uint16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (pm25 >= 0 && pm25 <= 1000)
                    d.pm25 = pm25;
            } else if (object_type == bth_pm10_uint16) {
                float pm10 = static_cast<uint16_t>(data[pos] + (data[pos + 1] << 8));
                pos += 2;
                if (pm10 >= 0 && pm10 <= 1000)
                    d.pm10 = pm10;
            } else if (object_type == bth_voltage_p_uint16) {
                pos += 2; // voltage, unused
            } else if (object_type == bth_binary_power) {
                pos++; // power state, unused
            } else {
                if (object_length > 0)
                    pos += object_length; // skip known-size data
                else
                    break; // otherwise we are lost
            }
        }
    }

    return d;
}

SensorAdvData AdvertisementParser::decodeThermoBeacon(const QByteArray &mfrData)
{
    SensorAdvData d;
    const quint8 *data = reinterpret_cast<const quint8 *>(mfrData.constData());
    // 18-byte message = current temperature + humidity (raw / 16); the 20-byte
    // message holds max/min only and yields nothing.
    if (mfrData.size() == 18) {
        const float temp = static_cast<int16_t>(data[10] + (data[11] << 8)) / 16.f;
        const float humi = std::round(static_cast<uint16_t>(data[12] + (data[13] << 8)) / 16.f);
        if (temp > -30.f && temp < 100.f)
            d.temperature = temp;
        if (humi >= 0.f && humi <= 100.f)
            d.humidity = humi;
    }
    return d;
}

unsigned AdvertisementParser::objectMask(const SensorAdvData &d)
{
    unsigned m = 0;
    if (d.battery > -99) m |= ADV_BATTERY;
    if (d.temperature > -99.f) m |= ADV_TEMPERATURE;
    if (d.humidity > -99.f) m |= ADV_HUMIDITY;
    if (d.luminosity > -99) m |= ADV_LUMINOSITY;
    if (d.soilMoisture > -99) m |= ADV_MOISTURE;
    if (d.soilConductivity > -99) m |= ADV_CONDUCTIVITY;
    if (d.hcho > -99.f) m |= ADV_FORMALDEHYDE;
    if (d.pressure > -99.f) m |= ADV_PRESSURE;
    if (d.voc > -99.f) m |= ADV_VOC;
    if (d.co2 > -99.f) m |= ADV_CO2;
    if (d.pm25 > -99.f) m |= ADV_PM25;
    if (d.pm10 > -99.f) m |= ADV_PM10;
    return m;
}

uint16_t AdvertisementParser::xiaomiProductId(const QByteArray &ba)
{
    if (ba.size() < 4)
        return 0;
    const quint8 *d = reinterpret_cast<const quint8 *>(ba.constData());
    return static_cast<uint16_t>(d[2] + (d[3] << 8));
}

std::vector<Reading> toReadings(const SensorAdvData &d, const QDateTime &at)
{
    std::vector<Reading> out;
    const auto add = [&](Quantity q, double v, Unit u) {
        out.push_back(Reading { q, v, u, at, Provenance::Advertisement });
    };
    if (d.battery > -99)          add(Quantity::Battery, d.battery, Unit::Percent);
    if (d.temperature > -99.f)    add(Quantity::AirTemperature, d.temperature, Unit::DegreeCelsius);
    if (d.humidity > -99.f)       add(Quantity::AirHumidity, d.humidity, Unit::Percent);
    if (d.soilMoisture > -99)     add(Quantity::SoilMoisture, d.soilMoisture, Unit::Percent);
    if (d.soilConductivity > -99) add(Quantity::SoilConductivity, d.soilConductivity, Unit::MicroSiemensPerCm);
    if (d.luminosity > -99)       add(Quantity::Illuminance, d.luminosity, Unit::Lux);
    if (d.hcho > -99.f)           add(Quantity::Hcho, d.hcho, Unit::MicrogramPerCubicMetre);
    if (d.pressure > -99.f)       add(Quantity::Pressure, d.pressure, Unit::Hectopascal);
    if (d.voc > -99.f)            add(Quantity::Voc, d.voc, Unit::MicrogramPerCubicMetre);
    if (d.co2 > -99.f)            add(Quantity::Co2, d.co2, Unit::Ppm);
    if (d.pm25 > -99.f)           add(Quantity::Pm25, d.pm25, Unit::MicrogramPerCubicMetre);
    if (d.pm10 > -99.f)           add(Quantity::Pm10, d.pm10, Unit::MicrogramPerCubicMetre);
    return out;
}

AdvSnapshotAccumulator::Result AdvSnapshotAccumulator::feed(unsigned frameMask, int64_t nowMs)
{
    const unsigned relevant = frameMask & requiredMask;
    if (relevant == 0)
        return Partial;

    if (mask == 0 || windowStartMs < 0 || (nowMs - windowStartMs) > windowMs) {
        mask = relevant;
        windowStartMs = nowMs;
    } else {
        mask |= relevant;
    }

    if ((mask & requiredMask) == requiredMask) {
        reset();
        return Complete;
    }
    return Partial;
}

} // namespace klr
