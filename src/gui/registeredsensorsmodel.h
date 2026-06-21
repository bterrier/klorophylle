// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QtCore/QString>

namespace klr {

class ISensorRepository;
class IBindingRepository;
class IReadingRepository;
class BleScanner;
class HistorySyncController;
class Clock;

// Every registered sensor (the `sensors` table — bound AND completely unbound), as a thin
// per-row model for the "Registered sensors" section of the Sensors screen. Distinct
// from the live BLE-scan list (DiscoveredDevicesModel): these are sensors the app KNOWS,
// some of which may be offline / never heard this session. Each row carries the sensor's
// model + hardware address, whether it is currently bound to any plant (an open binding —
// the delete guard), its authoritative battery (from the reading store), live connectivity
// (Liveness via the scanner + injected clock), the last-seen text, and whether a GATT
// connection is open. No SQL, no setContextProperty — AppContext hands this to QML.
class RegisteredSensorsModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        SensorIdRole = Qt::UserRole + 1,
        ModelRole,
        AddressRole,   // raw BLE handle (MAC), disambiguates same-model sensors
        BoundRole,     // referenced by >=1 plant (any binding, open or closed) — blocks delete
        LivenessRole,  // Liveness (int): Offline/Stale/Live/Connected; -1 when not judgeable
        BatteryRole,   // formatted battery text, or "" when unknown
        LastSeenRole,  // relative "time since last heard" text
        GattOpenRole,  // a GATT connection (read / history sync) is open to this sensor
    };

    RegisteredSensorsModel(ISensorRepository &sensors, IBindingRepository &bindings,
                           IReadingRepository &readings, const Clock &clock,
                           BleScanner *scanner = nullptr,
                           HistorySyncController *historySync = nullptr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh(); // full reset: rebuild rows from the sensor table + re-read each battery

    // Re-judge each row's liveness + GATT-open from the scanner + clock and emit dataChanged
    // only for changed rows. Driven on a timer from the composition root (time-relative).
    Q_INVOKABLE void refreshConnectivity();

private:
    struct Row {
        SensorId sensor;
        QString model;
        QString address;
        bool bound = false;
        QString battery;
        int liveness = -1;
        bool gattOpen = false;
    };
    int livenessForHandle(const QString &handle) const;
    bool gattOpenForHandle(const QString &handle) const;

    ISensorRepository &m_sensors;
    IBindingRepository &m_bindings;
    IReadingRepository &m_readings;
    const Clock &m_clock;
    BleScanner *m_scanner = nullptr;
    HistorySyncController *m_historySync = nullptr;
    QList<Row> m_rows;
};

} // namespace klr
