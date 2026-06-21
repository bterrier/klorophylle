// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QList>

namespace klr {

// A platform-neutral snapshot of one advertisement's payloads — built by the
// scanner (klr_ble) from a QBluetoothDeviceInfo, so the decoders/devices stay
// free of any Qt Bluetooth dependency.
struct AdvertisementData {
    QHash<quint16, QByteArray> serviceData16;    // 16-bit service UUID -> bytes
    QHash<quint16, QByteArray> manufacturerData; // company id -> bytes
    QList<quint16> serviceUuids16;               // 16-bit advertised service UUIDs (e.g. ESS 0x181A)
};

} // namespace klr
