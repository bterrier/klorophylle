// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gattprofile.h" // klr_devices
#include "reading.h"     // klr_core

#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>

#include <vector>

QT_BEGIN_NAMESPACE
class QLowEnergyController;
class QLowEnergyService;
class QTimer;
QT_END_NAMESPACE

namespace klr {

// One-shot GATT reader: connect -> discover -> (optional trigger writes) ->
// read/subscribe the profile's characteristics -> decode -> disconnect. Drives a
// Qt-Bluetooth-free GattReadProfile (from klr_devices) so the per-device wire
// knowledge stays in the pure, unit-tested decoders. Only one read runs at a
// time. Always disconnects when done; on timeout it returns whatever it gathered
// (partial) rather than nothing.
class GattSession : public QObject {
    Q_OBJECT
public:
    explicit GattSession(QObject *parent = nullptr);
    ~GattSession() override;

    bool isBusy() const { return m_busy; }

    // Start a read. `id` is echoed back in the result signals (the scanner's
    // platform handle). No-op-with-failure if a read is already running.
    void read(const QString &id, const QBluetoothDeviceInfo &info, const GattReadProfile &profile);

signals:
    void finished(const QString &id, const std::vector<klr::Reading> &readings);
    void failed(const QString &id, const QString &message);
    void busyChanged(bool busy);

private:
    void onConnected();
    void onDiscoveryFinished();
    void onServiceDiscovered();
    void startReads();
    void onCharacteristic(const QString &uuid, const QByteArray &value);
    void succeed();
    void fail(const QString &message);
    void cleanup();
    // Tear down a controller without tripping BlueZ's "deleted in ClosingState" warning: delete it
    // now if already unconnected, else close it and deleteLater once it has disconnected.
    static void deleteControllerWhenClosed(QLowEnergyController *c);
    QString primaryService() const;
    QString serviceOf(const GattCharacteristicRead &r) const;

    bool m_busy = false;
    QString m_id;
    GattReadProfile m_profile;

    QLowEnergyController *m_controller = nullptr;
    QHash<QString, QLowEnergyService *> m_services; // canon service uuid -> object
    int m_servicesPending = 0;
    QTimer *m_timeout = nullptr;

    QSet<QString> m_expected; // characteristic UUIDs we still await (canonical)
    std::vector<Reading> m_readings;
};

} // namespace klr
