// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reading.h"

#include <QtCore/QDateTime>
#include <QtCore/QList>
#include <QtCore/QString>

namespace klr {

// A sensor seen broadcasting, with its latest value per quantity. Identity is
// the platform BLE handle (DeviceHandle in docs/adr/0001) — a MAC on
// desktop/Android, a CoreBluetooth UUID on Apple — stringified.
struct DiscoveredDevice {
    QString id;            // platform handle
    QString name;          // advertised name (may be empty)
    qint16 rssi = 0;
    QString model;         // matched Device subclass model, e.g. "Flower Care" (empty if unrecognised)
    bool canRead = false;  // matched device offers a one-shot GATT read (gattProfile present)
    bool canSyncHistory = false; // matched device offers a GATT history log (gattHistoryProfile present)
    QDateTime lastSeen;
    QList<Reading> latest; // latest reading per quantity, sorted by quantity
};

} // namespace klr
