// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QString>

namespace klr {

class BleScanner;
class HistorySyncController;
class Clock;

// Adapts BleScanner's device list to QML. Thin: it holds no data of its own,
// just mirrors the scanner's discovery order and reads through on demand. QML
// binds to this; it never sees the scanner or the domain types directly.
class DiscoveredDevicesModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        RssiRole,
        ModelRole,
        ValueCountRole,
        // True when the device is a recognised model or already broadcasts decodable
        // values — i.e. a sensor Klorophylle supports, vs. background BLE noise.
        SupportedRole,
        LivenessRole,    // Liveness (int): Offline/Stale/Live, from broadcast/value freshness
        LastSeenRole,    // relative text, e.g. "12s ago" (frozen once Offline)
        BatteryRole,     // broadcast battery formatted, or "" if the device doesn't broadcast it
        GattOpenRole,    // a GATT connection (read or history sync) is open to this device now
    };

    // `clock` judges liveness/last-seen against now (injected — no wall-clock read).
    DiscoveredDevicesModel(BleScanner &scanner, const Clock &clock, QObject *parent = nullptr);

    // Wire the history-sync controller after construction (it is built later in the
    // composition root) so a row can show "connected" while its history downloads.
    void setHistorySync(HistorySyncController *historySync) { m_historySync = historySync; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Re-judge each device's liveness + GATT-open from the scanner + clock, emitting
    // dataChanged only for rows that changed. Driven on a timer from the composition root,
    // so a device that stops broadcasting flips to Offline with no new event.
    Q_INVOKABLE void refreshConnectivity();

private:
    void onDeviceAdded(const QString &id);
    void onDeviceChanged(const QString &id);
    int livenessForId(const QString &id) const;
    bool gattOpenForId(const QString &id) const;

    BleScanner &m_scanner;
    const Clock &m_clock;
    HistorySyncController *m_historySync = nullptr;
    QList<QString> m_ids; // discovery order; row -> device id
    QHash<QString, int> m_liveness; // last-emitted liveness per id (change detection)
    QHash<QString, bool> m_gattOpen; // last-emitted GATT-open per id (change detection)
};

} // namespace klr
