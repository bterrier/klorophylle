// SPDX-License-Identifier: GPL-3.0-or-later
#include "gatthistorysession.h"

#include "historysync.h"     // klr_core: historyEntriesToRead
#include "log.h"
#include "mibeacon_auth.h"   // klr_devices: mibeaconHandshake

#include <QtBluetooth/QBluetoothAddress>
#include <QtBluetooth/QLowEnergyCharacteristic>
#include <QtBluetooth/QLowEnergyController>
#include <QtBluetooth/QLowEnergyService>
#include <QtCore/QDateTime>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QTimeZone>

namespace klr {

namespace {
QString canon(const QString &uuid) { return QBluetoothUuid(uuid).toString(); }
// The fixed MiBeacon "start session" command written before the challenge (WatchFlower's device_flowercare).
const QByteArray kHandshakeStartCmd = QByteArray::fromHex("90ca85de");
} // namespace

GattHistorySession::GattHistorySession(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        qCWarning(lcBle) << "GattHistorySession timeout for" << m_id << "entries=" << int(m_history.size());
        if (!m_history.empty() || !m_battery.empty())
            succeed(/*complete=*/false); // salvage battery + partial history; do NOT advance last-sync
        else
            fail(tr("Timed out reading history."));
    });
}

GattHistorySession::~GattHistorySession() { cleanup(); }

QLowEnergyService *GattHistorySession::svc(const QString &uuid) const
{
    return m_services.value(canon(uuid), nullptr);
}

void GattHistorySession::sync(const QString &id, const QBluetoothDeviceInfo &info,
                              const GattHistoryProfile &profile, std::optional<qint64> lastSyncMs,
                              qint64 nowMs)
{
    if (m_busy) {
        Q_EMIT failed(id, tr("A history sync is already in progress."));
        return;
    }
    m_busy = true;
    Q_EMIT busyChanged(true);
    m_id = id;
    m_profile = profile;
    m_lastSyncMs = lastSyncMs;
    m_nowMs = nowMs;
    m_history.clear();
    m_battery.clear();
    m_services.clear();
    m_servicesPending = 0;
    m_step = Step::Idle;
    m_wallEpochMs = 0;
    m_count = m_toRead = m_index = 0;

    // The 6-byte MAC in natural order, for the handshake token (empty/zero on platforms without a
    // MAC, e.g. Apple — history there is out of scope).
    const quint64 a = info.address().toUInt64();
    m_mac = QByteArray(6, 0);
    for (int i = 0; i < 6; ++i)
        m_mac[i] = char((a >> (8 * (5 - i))) & 0xffULL);

    m_controller = QLowEnergyController::createCentral(info, this);
    connect(m_controller, &QLowEnergyController::connected, this,
            [this] { m_controller->discoverServices(); });
    connect(m_controller, &QLowEnergyController::discoveryFinished, this,
            &GattHistorySession::onDiscoveryFinished);
    connect(m_controller, &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error e) {
                qCWarning(lcBle) << "GattHistorySession controller error" << int(e);
                fail(tr("Bluetooth connection failed."));
            });
    connect(m_controller, &QLowEnergyController::disconnected, this, [this] {
        // Reaching here while still busy means the link dropped mid-download: salvage battery + any
        // entries, but flag it incomplete so the backlog is retried rather than marked fully synced.
        if (m_busy) {
            if (!m_history.empty() || !m_battery.empty())
                succeed(/*complete=*/false);
            else
                fail(tr("Device disconnected before sending history."));
        }
    });

    m_timeoutMs = profile.timeoutMs > 0 ? profile.timeoutMs : 30000;
    m_timeout->start(m_timeoutMs);
    m_controller->connectToDevice();
}

void GattHistorySession::onDiscoveryFinished()
{
    // The services we need: history (control/data/time), battery, and the handshake service.
    QSet<QString> needed;
    needed.insert(canon(m_profile.service));
    if (!m_profile.batteryService.isEmpty())
        needed.insert(canon(m_profile.batteryService));
    if (m_profile.needsHandshake && !m_profile.handshakeService.isEmpty())
        needed.insert(canon(m_profile.handshakeService));

    for (const QString &uuid : needed) {
        const QBluetoothUuid su(uuid);
        if (!m_controller->services().contains(su)) {
            // Battery is best-effort; a missing history/handshake service is fatal.
            if (uuid == canon(m_profile.batteryService))
                continue;
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
        connect(service, &QLowEnergyService::characteristicRead, this,
                [this](const QLowEnergyCharacteristic &c, const QByteArray &v) {
                    onRead(c.uuid().toString(), v);
                });
        connect(service, &QLowEnergyService::characteristicWritten, this,
                [this](const QLowEnergyCharacteristic &c, const QByteArray &) {
                    onWritten(c.uuid().toString());
                });
    }

    if (!svc(m_profile.service)) {
        fail(tr("The device does not expose the history service."));
        return;
    }
    m_servicesPending = int(m_services.size());
    for (QLowEnergyService *service : std::as_const(m_services))
        service->discoverDetails();
}

void GattHistorySession::onServiceDiscovered()
{
    if (m_servicesPending > 0)
        --m_servicesPending;
    if (m_servicesPending == 0)
        beginAfterDiscovery();
}

void GattHistorySession::beginAfterDiscovery()
{
    if (m_profile.needsHandshake && svc(m_profile.handshakeService))
        doHandshakeStart();
    else
        enterHistoryMode();
}

void GattHistorySession::doHandshakeStart()
{
    QLowEnergyService *hs = svc(m_profile.handshakeService);
    const QLowEnergyCharacteristic ch =
        hs ? hs->characteristic(QBluetoothUuid(m_profile.handshakeStartCharacteristic))
           : QLowEnergyCharacteristic();
    if (!ch.isValid()) {
        // No handshake characteristic — try history mode directly (older firmware).
        enterHistoryMode();
        return;
    }
    m_step = Step::HandshakeStart;
    hs->writeCharacteristic(ch, kHandshakeStartCmd, QLowEnergyService::WriteWithResponse);
}

void GattHistorySession::enterHistoryMode()
{
    QLowEnergyService *h = svc(m_profile.service);
    const QLowEnergyCharacteristic ch =
        h ? h->characteristic(QBluetoothUuid(m_profile.controlCharacteristic)) : QLowEnergyCharacteristic();
    if (!ch.isValid()) {
        fail(tr("The history control characteristic is missing."));
        return;
    }
    m_step = Step::EnterMode;
    h->writeCharacteristic(ch, m_profile.modeCommand, QLowEnergyService::WriteWithResponse);
}

void GattHistorySession::requestEntry(int index)
{
    QLowEnergyService *h = svc(m_profile.service);
    const QLowEnergyCharacteristic ch =
        h ? h->characteristic(QBluetoothUuid(m_profile.controlCharacteristic)) : QLowEnergyCharacteristic();
    if (!ch.isValid() || !m_profile.addressFor) {
        succeed(); // can't address entries — battery was already read up front
        return;
    }
    m_step = Step::SelectEntry;
    h->writeCharacteristic(ch, m_profile.addressFor(index), QLowEnergyService::WriteWithResponse);
}

void GattHistorySession::readBattery()
{
    QLowEnergyService *bs = svc(m_profile.batteryService);
    const QLowEnergyCharacteristic ch =
        bs ? bs->characteristic(QBluetoothUuid(m_profile.batteryCharacteristic)) : QLowEnergyCharacteristic();
    if (!bs || !ch.isValid() || !m_profile.decodeBattery) {
        startEntries(); // no battery on this device — go straight to the history backlog
        return;
    }
    m_step = Step::ReadBattery;
    bs->readCharacteristic(ch);
}

void GattHistorySession::startEntries()
{
    if (m_toRead <= 0) {
        succeed();
        return;
    }
    m_index = 0;
    requestEntry(m_index);
}

void GattHistorySession::onWritten(const QString &uuid)
{
    Q_UNUSED(uuid)
    // Each write-confirm is progress; treat the timeout as an inactivity watchdog so a long but
    // healthy download (hundreds of entries, each a write+read round trip) isn't cut short.
    m_timeout->start(m_timeoutMs);
    switch (m_step) {
    case Step::HandshakeStart: {
        // Start acknowledged → send the challenge token on the auth characteristic.
        const MiBeaconHandshake hsk = mibeaconHandshake(m_mac, m_profile.productId);
        QLowEnergyService *hs = svc(m_profile.handshakeService);
        const QLowEnergyCharacteristic ch =
            hs ? hs->characteristic(QBluetoothUuid(m_profile.handshakeAuthCharacteristic))
               : QLowEnergyCharacteristic();
        if (!ch.isValid() || hsk.challenge.isEmpty()) {
            enterHistoryMode();
            return;
        }
        m_step = Step::HandshakeChallenge;
        hs->writeCharacteristic(ch, hsk.challenge, QLowEnergyService::WriteWithResponse);
        return;
    }
    case Step::HandshakeChallenge: {
        const MiBeaconHandshake hsk = mibeaconHandshake(m_mac, m_profile.productId);
        QLowEnergyService *hs = svc(m_profile.handshakeService);
        const QLowEnergyCharacteristic ch =
            hs ? hs->characteristic(QBluetoothUuid(m_profile.handshakeAuthCharacteristic))
               : QLowEnergyCharacteristic();
        m_step = Step::HandshakeFinish;
        hs->writeCharacteristic(ch, hsk.finish, QLowEnergyService::WriteWithResponse);
        return;
    }
    case Step::HandshakeFinish:
        enterHistoryMode();
        return;
    case Step::EnterMode: {
        // History mode entered → read the device clock to anchor entry timestamps.
        QLowEnergyService *h = svc(m_profile.service);
        const QLowEnergyCharacteristic ch =
            h ? h->characteristic(QBluetoothUuid(m_profile.deviceTimeCharacteristic))
              : QLowEnergyCharacteristic();
        if (!ch.isValid()) {
            fail(tr("The device-time characteristic is missing."));
            return;
        }
        m_step = Step::ReadDeviceTime;
        h->readCharacteristic(ch);
        return;
    }
    case Step::SelectEntry: {
        // Address selected → read the entry payload.
        QLowEnergyService *h = svc(m_profile.service);
        const QLowEnergyCharacteristic ch =
            h ? h->characteristic(QBluetoothUuid(m_profile.dataCharacteristic)) : QLowEnergyCharacteristic();
        m_step = Step::ReadEntry;
        h->readCharacteristic(ch);
        return;
    }
    default:
        return;
    }
}

void GattHistorySession::onRead(const QString &uuid, const QByteArray &value)
{
    Q_UNUSED(uuid)
    m_timeout->start(m_timeoutMs); // a value arrived — reset the inactivity watchdog
    switch (m_step) {
    case Step::ReadDeviceTime: {
        const qint64 uptimeSecs = m_profile.decodeDeviceTime ? m_profile.decodeDeviceTime(value) : 0;
        m_wallEpochMs = m_nowMs - uptimeSecs * 1000;
        // Now read the entry count (the data characteristic's first read).
        QLowEnergyService *h = svc(m_profile.service);
        const QLowEnergyCharacteristic ch =
            h ? h->characteristic(QBluetoothUuid(m_profile.dataCharacteristic)) : QLowEnergyCharacteristic();
        if (!ch.isValid()) {
            fail(tr("The history-data characteristic is missing."));
            return;
        }
        m_step = Step::ReadCount;
        h->readCharacteristic(ch);
        return;
    }
    case Step::ReadCount: {
        m_count = m_profile.decodeCount ? m_profile.decodeCount(value) : 0;
        m_toRead = historyEntriesToRead(m_count, m_lastSyncMs, m_nowMs, m_profile.entriesPerHour);
        qCInfo(lcBle) << "GattHistorySession" << m_id << "buffer=" << m_count << "fetching=" << m_toRead;
        // Read battery BEFORE the (potentially long) entry loop, so it is captured even when a large
        // backlog later runs into the watchdog/disconnect salvage path.
        readBattery();
        return;
    }
    case Step::ReadBattery:
        if (m_profile.decodeBattery)
            m_battery = m_profile.decodeBattery(value, QDateTime::currentDateTime());
        startEntries();
        return;
    case Step::ReadEntry: {
        if (m_profile.decodeEntry) {
            const std::vector<Reading> got = m_profile.decodeEntry(value, m_wallEpochMs);
            m_history.insert(m_history.end(), got.begin(), got.end());
        }
        Q_EMIT progress(m_id, m_index + 1, m_toRead);
        ++m_index;
        if (m_index < m_toRead)
            requestEntry(m_index);
        else
            succeed();
        return;
    }
    default:
        return;
    }
}

void GattHistorySession::succeed(bool complete)
{
    const QString id = m_id;
    std::vector<Reading> history = std::move(m_history);
    std::vector<Reading> battery = std::move(m_battery);
    const qint64 through = m_nowMs;
    cleanup();
    qCDebug(lcBle) << "GattHistorySession finished" << id << "entries=" << int(history.size())
                   << "complete=" << complete;
    Q_EMIT finished(id, history, battery, through, complete);
}

void GattHistorySession::fail(const QString &message)
{
    const QString id = m_id;
    cleanup();
    Q_EMIT failed(id, message);
}

void GattHistorySession::cleanup()
{
    m_timeout->stop();
    m_step = Step::Idle;
    m_history.clear();
    m_battery.clear();
    m_servicesPending = 0;

    for (QLowEnergyService *service : std::as_const(m_services)) {
        service->disconnect(this);
        service->deleteLater();
    }
    m_services.clear();

    if (m_controller) {
        // Drop our slots first: disconnectFromDevice() can re-emit disconnected/errorOccurred.
        m_controller->disconnect(this);
        deleteControllerWhenClosed(m_controller);
        m_controller = nullptr;
    }

    if (m_busy) {
        m_busy = false;
        Q_EMIT busyChanged(false);
    }
}

void GattHistorySession::deleteControllerWhenClosed(QLowEnergyController *c)
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
