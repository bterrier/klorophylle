// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QDateTime>
#include <QtCore/QString>

namespace klr {

// Where a platform BLE handle came from. Stored beside the app-minted SensorId so
// dedup matches on the handle and never assumes a MAC: a desktop/Android MAC
// and an opaque Apple CoreBluetooth UUID are both just
// (kind, value) pairs here.
enum class HandleKind { Mac, CoreBluetoothUuid };

// A physical sensor — plant-agnostic. It owns identity + the raw handle and nothing
// else: never a plant, never thresholds (those live on the plant/care side). The
// plant<->sensor relation is the time-bounded PlantSensorBinding (binding.h), not a
// field here, so one sensor can serve several plants (shared pot).
struct Sensor {
    SensorId id {};
    QString model;
    HandleKind handleKind { HandleKind::Mac };
    QString handleValue;            // raw MAC / CoreBluetooth UUID
    QDateTime firstSeen {};

    bool operator==(const Sensor &) const = default;
};

} // namespace klr
