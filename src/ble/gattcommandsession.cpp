// SPDX-License-Identifier: GPL-3.0-or-later
#include "gattcommandsession.h"

#include "log.h"
#include "mibeacon_auth.h" // klr_devices: mibeaconHandshake

#include <QtBluetooth/QBluetoothAddress>
#include <QtBluetooth/QBluetoothUuid>
#include <QtBluetooth/QLowEnergyCharacteristic>
#include <QtBluetooth/QLowEnergyController>
#include <QtBluetooth/QLowEnergyService>
#include <QtCore/QSet>
#include <QtCore/QTimer>

namespace klr {

namespace {
QString canon(const QString &uuid) { return QBluetoothUuid(uuid).toString(); }
// The fixed MiBeacon "start session" command written before the challenge (same as the history
// handshake — WatchFlower's device_flowercare).
const QByteArray kHandshakeStartCmd = QByteArray::fromHex("90ca85de");
} // namespace

GattCommandSession::GattCommandSession(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        qCWarning(lcBle) << "GattCommandSession timeout for" << m_id;
        fail(tr("Timed out sending the command."));
    });
}

GattCommandSession::~GattCommandSession() { cleanup(); }

QLowEnergyService *GattCommandSession::svc(const QString &uuid) const
{
    return m_services.value(canon(uuid), nullptr);
}

void GattCommandSession::run(const QString &id, const QBluetoothDeviceInfo &info,
                             const GattCommand &command)
{
    if (m_busy) {
        Q_EMIT failed(id, tr("A command is already in progress."));
        return;
    }
    m_busy = true;
    Q_EMIT busyChanged(true);
    m_id = id;
    m_command = command;
    m_services.clear();
    m_servicesPending = 0;
    m_step = Step::Idle;

    // The 6-byte MAC in natural order, for the handshake token (empty/zero on platforms without a
    // MAC, e.g. Apple — out of scope here).
    const quint64 a = info.address().toUInt64();
    m_mac = QByteArray(6, 0);
    for (int i = 0; i < 6; ++i)
        m_mac[i] = char((a >> (8 * (5 - i))) & 0xffULL);

    m_controller = QLowEnergyController::createCentral(info, this);
    connect(m_controller, &QLowEnergyController::connected, this,
            [this] { m_controller->discoverServices(); });
    connect(m_controller, &QLowEnergyController::discoveryFinished, this,
            &GattCommandSession::onDiscoveryFinished);
    connect(m_controller, &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error e) {
                qCWarning(lcBle) << "GattCommandSession controller error" << int(e);
                fail(tr("Bluetooth connection failed."));
            });
    connect(m_controller, &QLowEnergyController::disconnected, this, [this] {
        if (m_busy)
            fail(tr("Device disconnected before the command completed."));
    });

    m_timeout->start(m_timeoutMs);
    m_controller->connectToDevice();
}

void GattCommandSession::onDiscoveryFinished()
{
    // The services we need: the command's target service, plus the handshake service when gated.
    QSet<QString> needed;
    needed.insert(canon(m_command.service));
    if (m_command.handshake && !m_command.handshake->service.isEmpty())
        needed.insert(canon(m_command.handshake->service));

    for (const QString &uuid : needed) {
        const QBluetoothUuid su(uuid);
        if (!m_controller->services().contains(su)) {
            fail(tr("The device does not expose the expected service."));
            return;
        }
        QLowEnergyService *service = m_controller->createServiceObject(su, this);
        if (!service)
            continue;
        m_services.insert(uuid, service);
        connect(service, &QLowEnergyService::stateChanged, this,
                [this](QLowEnergyService::ServiceState s) {
                    if (s == QLowEnergyService::RemoteServiceDiscovered)
                        onServiceDiscovered();
                });
        connect(service, &QLowEnergyService::characteristicWritten, this,
                [this](const QLowEnergyCharacteristic &c, const QByteArray &) {
                    onWritten(c.uuid().toString());
                });
    }

    if (!svc(m_command.service)) {
        fail(tr("The device does not expose the expected service."));
        return;
    }
    m_servicesPending = int(m_services.size());
    for (QLowEnergyService *service : std::as_const(m_services))
        service->discoverDetails();
}

void GattCommandSession::onServiceDiscovered()
{
    if (m_servicesPending > 0)
        --m_servicesPending;
    if (m_servicesPending == 0)
        beginAfterDiscovery();
}

void GattCommandSession::beginAfterDiscovery()
{
    if (m_command.handshake && svc(m_command.handshake->service))
        doHandshakeStart();
    else
        writeCommand();
}

void GattCommandSession::doHandshakeStart()
{
    QLowEnergyService *hs = svc(m_command.handshake->service);
    const QLowEnergyCharacteristic ch =
        hs ? hs->characteristic(QBluetoothUuid(m_command.handshake->startCharacteristic))
           : QLowEnergyCharacteristic();
    if (!ch.isValid()) {
        // No handshake characteristic — try the write directly (older firmware).
        writeCommand();
        return;
    }
    m_step = Step::HandshakeStart;
    hs->writeCharacteristic(ch, kHandshakeStartCmd, QLowEnergyService::WriteWithResponse);
}

void GattCommandSession::writeCommand()
{
    QLowEnergyService *service = svc(m_command.service);
    const QLowEnergyCharacteristic ch =
        service ? service->characteristic(QBluetoothUuid(m_command.characteristic))
                : QLowEnergyCharacteristic();
    if (!service || !ch.isValid()) {
        fail(tr("The command characteristic is missing."));
        return;
    }
    m_step = Step::WriteCommand;
    const auto mode = m_command.writeWithoutResponse ? QLowEnergyService::WriteWithoutResponse
                                                     : QLowEnergyService::WriteWithResponse;
    service->writeCharacteristic(ch, m_command.payload, mode);
}

void GattCommandSession::onWritten(const QString &uuid)
{
    Q_UNUSED(uuid)
    m_timeout->start(m_timeoutMs); // each write-confirm is progress
    switch (m_step) {
    case Step::HandshakeStart: {
        // Start acknowledged → send the challenge token on the auth characteristic.
        const MiBeaconHandshake hsk = mibeaconHandshake(m_mac, m_command.handshake->productId);
        QLowEnergyService *hs = svc(m_command.handshake->service);
        const QLowEnergyCharacteristic ch =
            hs ? hs->characteristic(QBluetoothUuid(m_command.handshake->authCharacteristic))
               : QLowEnergyCharacteristic();
        if (!ch.isValid() || hsk.challenge.isEmpty()) {
            writeCommand();
            return;
        }
        m_step = Step::HandshakeChallenge;
        hs->writeCharacteristic(ch, hsk.challenge, QLowEnergyService::WriteWithResponse);
        return;
    }
    case Step::HandshakeChallenge: {
        const MiBeaconHandshake hsk = mibeaconHandshake(m_mac, m_command.handshake->productId);
        QLowEnergyService *hs = svc(m_command.handshake->service);
        const QLowEnergyCharacteristic ch =
            hs ? hs->characteristic(QBluetoothUuid(m_command.handshake->authCharacteristic))
               : QLowEnergyCharacteristic();
        m_step = Step::HandshakeFinish;
        hs->writeCharacteristic(ch, hsk.finish, QLowEnergyService::WriteWithResponse);
        return;
    }
    case Step::HandshakeFinish:
        writeCommand();
        return;
    case Step::WriteCommand:
        succeed();
        return;
    default:
        return;
    }
}

void GattCommandSession::succeed()
{
    const QString id = m_id;
    cleanup();
    qCDebug(lcBle) << "GattCommandSession finished" << id;
    Q_EMIT finished(id);
}

void GattCommandSession::fail(const QString &message)
{
    const QString id = m_id;
    cleanup();
    Q_EMIT failed(id, message);
}

void GattCommandSession::cleanup()
{
    m_timeout->stop();
    m_step = Step::Idle;
    m_servicesPending = 0;

    for (QLowEnergyService *service : std::as_const(m_services)) {
        service->disconnect(this);
        service->deleteLater();
    }
    m_services.clear();

    if (m_controller) {
        m_controller->disconnect(this);
        deleteControllerWhenClosed(m_controller);
        m_controller = nullptr;
    }

    if (m_busy) {
        m_busy = false;
        Q_EMIT busyChanged(false);
    }
}

void GattCommandSession::deleteControllerWhenClosed(QLowEnergyController *c)
{
    if (c->state() == QLowEnergyController::UnconnectedState) {
        c->deleteLater();
        return;
    }
    // Deleting a controller mid-disconnect makes BlueZ warn and can leak the platform handle.
    // Detach it, close the link, and delete only once it has actually closed.
    c->setParent(nullptr);
    connect(c, &QLowEnergyController::disconnected, c, &QObject::deleteLater);
    connect(c, &QLowEnergyController::errorOccurred, c, [c] { c->deleteLater(); });
    QTimer::singleShot(10000, c, &QObject::deleteLater);
    c->disconnectFromDevice();
}

} // namespace klr
