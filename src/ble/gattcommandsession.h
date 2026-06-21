// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gattprofile.h" // GattCommand (klr_devices)

#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>

QT_BEGIN_NAMESPACE
class QLowEnergyController;
class QLowEnergyService;
class QTimer;
QT_END_NAMESPACE

namespace klr {

// One-shot GATT command writer (ADR 0026), sibling to GattSession / GattHistorySession:
// connect → discover → (optional MiBeacon RC4 handshake) → write the command payload → disconnect.
// It executes a single GattCommand (a device write action ported from WatchFlower: LED blink, watering,
// calibrate, clear the on-device log, clock-sync). The pure wire knowledge — the payload bytes and
// the handshake fields — lives in klr_devices (Device::gattCommands); only the stateful BLE machine
// is here. Like the other sessions there is no BLE in CI, so this is hardware-verified. One command
// at a time.
class GattCommandSession : public QObject {
    Q_OBJECT
public:
    explicit GattCommandSession(QObject *parent = nullptr);
    ~GattCommandSession() override;

    bool isBusy() const { return m_busy; }

    // Run a command. `id` is echoed back. No-op-with-failure if one is already running.
    void run(const QString &id, const QBluetoothDeviceInfo &info, const GattCommand &command);

signals:
    void finished(const QString &id);
    void failed(const QString &id, const QString &message);
    void busyChanged(bool busy);

private:
    enum class Step {
        Idle,
        HandshakeStart,     // wrote the start command, awaiting its write-confirm
        HandshakeChallenge, // wrote the challenge, awaiting its write-confirm
        HandshakeFinish,    // wrote the finish token, awaiting its write-confirm
        WriteCommand,       // wrote the command payload, awaiting its write-confirm
    };

    void onDiscoveryFinished();
    void onServiceDiscovered();
    void beginAfterDiscovery();
    void doHandshakeStart();
    void writeCommand();
    void onWritten(const QString &uuid);
    void succeed();
    void fail(const QString &message);
    void cleanup();
    static void deleteControllerWhenClosed(QLowEnergyController *c);
    QLowEnergyService *svc(const QString &uuid) const;

    bool m_busy = false;
    QString m_id;
    GattCommand m_command;
    QByteArray m_mac; // 6 bytes, natural order (for the handshake token)
    int m_timeoutMs = 12000;

    QLowEnergyController *m_controller = nullptr;
    QHash<QString, QLowEnergyService *> m_services; // canonical service uuid -> object
    int m_servicesPending = 0;
    QTimer *m_timeout = nullptr;
    Step m_step = Step::Idle;
};

} // namespace klr
