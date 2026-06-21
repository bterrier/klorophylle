// SPDX-License-Identifier: GPL-3.0-or-later
#include "gattsession.h"

#include "log.h"

#include <QtBluetooth/QLowEnergyCharacteristic>
#include <QtBluetooth/QLowEnergyController>
#include <QtBluetooth/QLowEnergyDescriptor>
#include <QtBluetooth/QLowEnergyService>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>

namespace klr {

// Canonical key for a UUID string ("{0000....}" lowercase), so 16/128-bit and
// case variants compare equal.
static QString canon(const QString &uuid)
{
    return QBluetoothUuid(uuid).toString();
}

GattSession::GattSession(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        qCWarning(lcBle) << "GattSession timeout for" << m_id << "partial=" << int(m_readings.size());
        // A timeout still yields whatever we managed to read.
        if (!m_readings.empty())
            succeed();
        else
            fail(tr("Timed out reading the device."));
    });
}

GattSession::~GattSession()
{
    cleanup();
}

QString GattSession::primaryService() const
{
    return canon(m_profile.service);
}

QString GattSession::serviceOf(const GattCharacteristicRead &r) const
{
    return canon(r.service.isEmpty() ? m_profile.service : r.service);
}

void GattSession::read(const QString &id, const QBluetoothDeviceInfo &info, const GattReadProfile &profile)
{
    if (m_busy) {
        Q_EMIT failed(id, tr("A read is already in progress."));
        return;
    }

    m_busy = true;
    Q_EMIT busyChanged(true);
    m_id = id;
    m_profile = profile;
    m_readings.clear();
    m_expected.clear();
    m_services.clear();
    m_servicesPending = 0;

    m_controller = QLowEnergyController::createCentral(info, this);
    connect(m_controller, &QLowEnergyController::connected, this, &GattSession::onConnected);
    connect(m_controller, &QLowEnergyController::discoveryFinished, this, &GattSession::onDiscoveryFinished);
    connect(m_controller, &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error e) {
                qCWarning(lcBle) << "GattSession controller error" << int(e);
                fail(tr("Bluetooth connection failed."));
            });
    connect(m_controller, &QLowEnergyController::disconnected, this, [this] {
        // Some sensors drop the link right after delivering data; if we got
        // anything, treat that as success rather than an error.
        if (m_busy) {
            if (!m_readings.empty())
                succeed();
            else
                fail(tr("Device disconnected before sending values."));
        }
    });

    m_timeout->start(profile.timeoutMs > 0 ? profile.timeoutMs : 12000);
    m_controller->connectToDevice();
}

void GattSession::onConnected()
{
    qCDebug(lcBle) << "GattSession connected" << m_id << "- discovering services";
    m_controller->discoverServices();
}

void GattSession::onDiscoveryFinished()
{
    // Every distinct service the profile needs (primary + per-read overrides).
    QSet<QString> needed;
    needed.insert(primaryService());
    for (const GattCharacteristicRead &r : m_profile.reads)
        needed.insert(serviceOf(r));

    for (const QString &svc : needed) {
        const QBluetoothUuid serviceUuid(svc);
        if (!m_controller->services().contains(serviceUuid)) {
            fail(tr("The device does not expose the expected service."));
            return;
        }
        QLowEnergyService *service = m_controller->createServiceObject(serviceUuid, this);
        if (!service) {
            fail(tr("Could not open the device service."));
            return;
        }
        m_services.insert(svc, service);
        connect(service, &QLowEnergyService::stateChanged, this,
                [this](QLowEnergyService::ServiceState s) {
                    if (s == QLowEnergyService::RemoteServiceDiscovered)
                        onServiceDiscovered();
                });
        connect(service, &QLowEnergyService::characteristicRead, this,
                [this](const QLowEnergyCharacteristic &c, const QByteArray &v) {
                    onCharacteristic(c.uuid().toString(), v);
                });
        connect(service, &QLowEnergyService::characteristicChanged, this,
                [this](const QLowEnergyCharacteristic &c, const QByteArray &v) {
                    onCharacteristic(c.uuid().toString(), v);
                });
    }

    m_servicesPending = int(m_services.size());
    for (QLowEnergyService *service : std::as_const(m_services))
        service->discoverDetails();
}

void GattSession::onServiceDiscovered()
{
    if (m_servicesPending > 0)
        --m_servicesPending;
    if (m_servicesPending == 0)
        startReads();
}

void GattSession::startReads()
{
    // Optional trigger writes (e.g. WP6003 request-notify + clock sync), on the
    // primary service.
    if (QLowEnergyService *primary = m_services.value(primaryService(), nullptr)) {
        for (const GattTrigger &t : m_profile.triggers) {
            const QLowEnergyCharacteristic ch = primary->characteristic(QBluetoothUuid(t.characteristic));
            if (!ch.isValid())
                continue;
            primary->writeCharacteristic(ch, t.makeValue ? t.makeValue() : QByteArray(),
                                         t.writeWithoutResponse ? QLowEnergyService::WriteWithoutResponse
                                                                : QLowEnergyService::WriteWithResponse);
        }
    }

    // Read once / subscribe to every characteristic that exists on this device.
    for (const GattCharacteristicRead &r : m_profile.reads) {
        QLowEnergyService *service = m_services.value(serviceOf(r), nullptr);
        if (!service)
            continue;
        const QLowEnergyCharacteristic ch = service->characteristic(QBluetoothUuid(r.characteristic));
        if (!ch.isValid())
            continue; // absent on this unit (e.g. an optional ESS quantity) — skip

        m_expected.insert(canon(r.characteristic));
        if (r.access == GattCharacteristicRead::Access::Notify) {
            const QLowEnergyDescriptor cccd = ch.clientCharacteristicConfiguration();
            if (cccd.isValid())
                service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
        } else {
            service->readCharacteristic(ch);
        }
    }

    if (m_expected.isEmpty())
        fail(tr("No readable values on this device."));
}

void GattSession::onCharacteristic(const QString &uuid, const QByteArray &value)
{
    const QString key = canon(uuid);
    if (!m_expected.contains(key))
        return;

    std::vector<Reading> got;
    for (const GattCharacteristicRead &r : m_profile.reads) {
        if (canon(r.characteristic) != key)
            continue;
        if (r.decode)
            got = r.decode(value, QDateTime::currentDateTime());
        break;
    }

    // A notify characteristic may emit framing/ack packets before the real data
    // (e.g. WP6003). Only count a characteristic as satisfied once it yields a
    // value; otherwise keep waiting (the timeout is the safety net).
    if (got.empty())
        return;

    m_readings.insert(m_readings.end(), got.begin(), got.end());
    m_expected.remove(key);
    if (m_expected.isEmpty())
        succeed();
}

void GattSession::succeed()
{
    const QString id = m_id;
    std::vector<Reading> readings = std::move(m_readings);
    cleanup();
    qCDebug(lcBle) << "GattSession finished" << id << "readings=" << int(readings.size());
    Q_EMIT finished(id, readings);
}

void GattSession::fail(const QString &message)
{
    const QString id = m_id;
    cleanup();
    Q_EMIT failed(id, message);
}

void GattSession::cleanup()
{
    m_timeout->stop();
    m_expected.clear();
    m_readings.clear();
    m_servicesPending = 0;

    for (QLowEnergyService *service : std::as_const(m_services)) {
        service->disconnect(this);
        service->deleteLater();
    }
    m_services.clear();

    if (m_controller) {
        // Drop our slots first: disconnectFromDevice() can re-emit disconnected/
        // errorOccurred synchronously, which would reenter succeed()/fail().
        m_controller->disconnect(this);
        deleteControllerWhenClosed(m_controller);
        m_controller = nullptr;
    }

    if (m_busy) {
        m_busy = false;
        Q_EMIT busyChanged(false);
    }
}

void GattSession::deleteControllerWhenClosed(QLowEnergyController *c)
{
    if (c->state() == QLowEnergyController::UnconnectedState) {
        c->deleteLater();
        return;
    }
    // Deleting a controller mid-disconnect makes BlueZ warn ("not Unconnected when deleted ...
    // ClosingState") and can leak the platform handle. Detach it from us (so it outlives our own
    // destruction), close the link, and delete only once it has actually closed — with a safety
    // timer in case the disconnected signal never arrives.
    c->setParent(nullptr);
    connect(c, &QLowEnergyController::disconnected, c, &QObject::deleteLater);
    connect(c, &QLowEnergyController::errorOccurred, c, [c] { c->deleteLater(); });
    QTimer::singleShot(10000, c, &QObject::deleteLater);
    c->disconnectFromDevice();
}

} // namespace klr
