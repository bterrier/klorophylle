// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QtCore/QString>

#include <optional>

namespace klr {

class ISensorRepository;
class IBindingRepository;
class IReadingRepository;
class BleScanner;
class HistorySyncController;
class Clock;

// The selected plant's bound sensors, as a thin per-row status model for the plant-detail
// "Sensors" tab. Read-only: pairing/detach stay in plant settings; a row links to the
// sensor-detail page. Each row carries the sensor's model + hardware address, when it was
// bound, its live connectivity (Liveness, judged via the scanner's broadcast/value freshness
// + the injected clock), its authoritative battery (from the reading store — Flower Care only
// reports battery over a connection), the last-seen text, and whether a GATT connection is
// open to it right now. No SQL, no setContextProperty — AppContext hands this to QML.
class SensorStatusModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        SensorIdRole = Qt::UserRole + 1,
        ModelRole,
        AddressRole,   // raw BLE handle (MAC), disambiguates same-model sensors
        SinceRole,     // localized bind time
        RoleRole,      // the binding's restricted quantity label, or "" (any)
        LivenessRole,  // Liveness (int): Offline/Stale/Live; -1 when not judgeable
        BatteryRole,   // formatted battery text, or "" when unknown
        LastSeenRole,  // relative "time since last heard" text
        GattOpenRole,  // a GATT connection (read / history sync) is open to this sensor
    };

    SensorStatusModel(ISensorRepository &sensors, IBindingRepository &bindings,
                      IReadingRepository &readings, const Clock &clock,
                      BleScanner *scanner = nullptr, HistorySyncController *historySync = nullptr,
                      QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Point the model at a plant (nullopt clears it), then rebuild its bound-sensor rows.
    void setPlant(std::optional<PlantId> plant);
    void refresh(); // full reset: rebuild rows + re-read each sensor's battery

    // Re-judge each row's liveness + GATT-open from the scanner + clock and emit dataChanged
    // only for changed rows. Driven on a timer from the composition root (time-relative).
    Q_INVOKABLE void refreshConnectivity();

private:
    // One bound sensor's static + cached-status fields. liveness/gattOpen are the last-emitted
    // values, used by refreshConnectivity() for change detection (data() computes live ones).
    struct Row {
        SensorId sensor;
        QString model;
        QString address;
        QString since;
        QString role;
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
    std::optional<PlantId> m_plant;
    QList<Row> m_rows;
};

} // namespace klr
