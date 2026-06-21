// SPDX-License-Identifier: GPL-3.0-or-later
#include "blescanner.h"

#include "device.h"
#include "deviceregistry.h"
#include "gatthistorysession.h"
#include "gattsession.h"
#include "log.h"

#include <QtBluetooth/QBluetoothAddress>
#include <QtBluetooth/QBluetoothDeviceDiscoveryAgent>
#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QtBluetooth/QBluetoothUuid>
#include <QtCore/QCoreApplication>
#include <QtCore/QPermissions>
#include <QtCore/QTimer>
#include <QtCore/QUuid>

#include <algorithm>

namespace klr {

BleScanner::BleScanner(const DeviceRegistry &registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_agent(new QBluetoothDeviceDiscoveryAgent(this))
{
    m_agent->setLowEnergyDiscoveryTimeout(0); // 0 = monitor continuously until stop()

    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
            [this](const QBluetoothDeviceInfo &info) { ingest(info); });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
            [this](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { ingest(info); });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](QBluetoothDeviceDiscoveryAgent::Error error) {
                qCWarning(lcBle) << "errorOccurred" << int(error) << m_agent->errorString();
                Q_EMIT errorOccurred(m_agent->errorString());
                Q_EMIT scanningChanged(false);
            });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this,
            [this] { Q_EMIT scanningChanged(false); });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this,
            [this] { Q_EMIT scanningChanged(false); });
}

BleScanner::~BleScanner() = default;

bool BleScanner::isScanning() const
{
    return m_agent->isActive();
}

void BleScanner::start()
{
    if (m_agent->isActive())
        return;

    // Permission is a no-op-Granted on desktop Linux, real on Android/Apple.
    auto *app = QCoreApplication::instance();
    const QBluetoothPermission permission;
    const Qt::PermissionStatus status = app->checkPermission(permission);
    qCDebug(lcBle) << "start permission=" << int(status);
    switch (status) {
    case Qt::PermissionStatus::Undetermined:
        app->requestPermission(permission, this, [this](const QPermission &result) {
            if (result.status() == Qt::PermissionStatus::Granted)
                startNow();
            else
                Q_EMIT errorOccurred(tr("Bluetooth permission was denied."));
        });
        return;
    case Qt::PermissionStatus::Denied:
        Q_EMIT errorOccurred(tr("Bluetooth permission was denied."));
        return;
    case Qt::PermissionStatus::Granted:
        startNow();
        return;
    }
}

void BleScanner::startNow()
{
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    qCDebug(lcBle) << "startNow isActive=" << m_agent->isActive()
                   << "error=" << m_agent->errorString();
    Q_EMIT scanningChanged(m_agent->isActive());
}

void BleScanner::stop()
{
    if (m_agent->isActive())
        m_agent->stop();
}

void BleScanner::pauseScanForGatt()
{
    if (m_agent->isActive()) {
        m_agent->stop();
        m_scanPausedForGatt = true;
        qCDebug(lcBle) << "paused discovery for a GATT session";
    }
}

void BleScanner::scheduleScanResume()
{
    // Defer: history sweeps run sensors back-to-back (the next sync is queued), so a short delay
    // lets the next session re-pause instead of flapping the radio between every device. Re-check
    // at fire time that nothing is using the radio.
    QTimer::singleShot(2000, this, [this] {
        if (!m_scanPausedForGatt)
            return;
        const bool busy = (m_gatt && m_gatt->isBusy())
                || (m_historySession && m_historySession->isBusy());
        if (busy)
            return; // another session grabbed the radio; it will schedule its own resume
        m_scanPausedForGatt = false;
        startNow();
        qCDebug(lcBle) << "resumed discovery after GATT session(s)";
    });
}

QList<DiscoveredDevice> BleScanner::devices() const
{
    QList<DiscoveredDevice> out;
    out.reserve(m_order.size());
    for (const QString &id : m_order)
        out.append(m_devices.value(id));
    return out;
}

const DiscoveredDevice *BleScanner::device(const QString &id) const
{
    const auto it = m_devices.constFind(id);
    return it == m_devices.cend() ? nullptr : &it.value();
}

void BleScanner::ingest(const QBluetoothDeviceInfo &info)
{
    // Build the platform-neutral advertisement snapshot for the pure decoder.
    AdvertisementData adv;
    const auto serviceData = info.serviceData();
    for (auto it = serviceData.constBegin(); it != serviceData.constEnd(); ++it) {
        bool ok = false;
        const quint16 uuid16 = it.key().toUInt16(&ok);
        if (ok)
            adv.serviceData16.insert(uuid16, it.value());
    }
    const auto manufacturerData = info.manufacturerData();
    for (auto it = manufacturerData.constBegin(); it != manufacturerData.constEnd(); ++it)
        adv.manufacturerData.insert(it.key(), it.value());

    // Advertised 16-bit service UUIDs (no service data) — e.g. ESS sensors that
    // announce 0x181A but carry their values only over GATT.
    const auto serviceUuids = info.serviceUuids();
    for (const QBluetoothUuid &u : serviceUuids) {
        bool ok = false;
        const quint16 uuid16 = u.toUInt16(&ok);
        if (ok)
            adv.serviceUuids16.append(uuid16);
    }

    // Identity = platform handle: MAC, or CoreBluetooth UUID on Apple.
    const QString id = info.address().isNull()
        ? info.deviceUuid().toString(QUuid::WithoutBraces)
        : info.address().toString();
    if (id.isEmpty())
        return;
    m_infos.insert(id, info); // retained so readValue() can open a GATT connection

    // Pick (once) the Device subclass that matches this advertisement, then let it
    // decode. Cached per id; re-attempted until a match is found.
    Device *impl = m_impls.value(id, nullptr);
    if (!impl) {
        auto created = m_registry.create(AdvertisementContext { info.name(), adv });
        if (created) {
            impl = created.get();
            m_ownedImpls.push_back(std::move(created));
            m_impls.insert(id, impl);
        }
    }
    const std::vector<Reading> readings =
        impl ? impl->parseAdvertisement(adv, QDateTime::currentDateTime()) : std::vector<Reading> {};

    const bool known = m_devices.contains(id);
    // Surface every discovered LE device (this is a scanner): the user picks one
    // to inspect. Devices we can decode also carry live values; the rest show 0.
    DiscoveredDevice &d = m_devices[id];
    d.id = id;
    if (!info.name().isEmpty())
        d.name = info.name();
    d.rssi = info.rssi();
    d.lastSeen = QDateTime::currentDateTime();
    if (impl) {
        d.model = impl->model();
        d.canRead = impl->gattProfile().has_value();
        d.canSyncHistory = impl->gattHistoryProfile().has_value();
    }

    mergeReadings(id, readings);

    if (known)
        Q_EMIT deviceChanged(id);
    else {
        m_order.append(id);
        qCDebug(lcBle) << "device discovered" << id << "name=" << d.name
                       << "readings=" << int(readings.size());
        Q_EMIT deviceAdded(id);
    }
}

void BleScanner::mergeReadings(const QString &id, const std::vector<Reading> &readings)
{
    if (readings.empty())
        return;

    DiscoveredDevice &d = m_devices[id];
    // Merge: latest value wins per quantity; keep sorted by quantity.
    for (const Reading &r : readings) {
        auto pos = std::find_if(d.latest.begin(), d.latest.end(),
                                [&](const Reading &e) { return e.quantity == r.quantity; });
        if (pos == d.latest.end())
            d.latest.append(r);
        else
            *pos = r;
    }
    std::sort(d.latest.begin(), d.latest.end(), [](const Reading &a, const Reading &b) {
        return static_cast<int>(a.quantity) < static_cast<int>(b.quantity);
    });
}

bool BleScanner::isReading() const
{
    return m_gatt && m_gatt->isBusy();
}

void BleScanner::readValue(const QString &id)
{
    Device *impl = m_impls.value(id, nullptr);
    const auto infoIt = m_infos.constFind(id);
    if (!impl || infoIt == m_infos.cend()) {
        Q_EMIT readFailed(id, tr("This device cannot be read."));
        return;
    }
    const std::optional<GattReadProfile> profile = impl->gattProfile();
    if (!profile) {
        Q_EMIT readFailed(id, tr("This device has no readable values over a connection."));
        return;
    }

    if (!m_gatt) {
        m_gatt = new GattSession(this);
        connect(m_gatt, &GattSession::busyChanged, this, &BleScanner::readingChanged);
        // The connection closed when the session goes idle — forget the target so a
        // per-sensor "connected" badge clears for the right row.
        connect(m_gatt, &GattSession::busyChanged, this, [this](bool busy) {
            if (!busy && !m_gattId.isEmpty()) {
                m_gattId.clear();
                Q_EMIT gattTargetChanged(activeGattId());
            }
            if (!busy)
                scheduleScanResume();
        });
        connect(m_gatt, &GattSession::failed, this, &BleScanner::readFailed);
        connect(m_gatt, &GattSession::finished, this,
                [this](const QString &fid, const std::vector<Reading> &readings) {
                    mergeReadings(fid, readings);
                    if (m_devices.contains(fid))
                        Q_EMIT deviceChanged(fid);
                });
    }

    qCDebug(lcBle) << "readValue" << id << "model=" << impl->model();
    pauseScanForGatt(); // BlueZ won't reliably connect while discovery is running
    m_gattId = id;
    Q_EMIT gattTargetChanged(activeGattId());
    m_gatt->read(id, infoIt.value(), *profile);
}

void BleScanner::syncHistory(const QString &id, std::optional<qint64> lastSyncMs, qint64 nowMs)
{
    Device *impl = m_impls.value(id, nullptr);
    const auto infoIt = m_infos.constFind(id);
    if (!impl || infoIt == m_infos.cend()) {
        Q_EMIT historySyncFailed(id, tr("This device cannot be reached."));
        return;
    }
    const std::optional<GattHistoryProfile> profile = impl->gattHistoryProfile();
    if (!profile) {
        Q_EMIT historySyncFailed(id, tr("This device has no history log."));
        return;
    }

    if (!m_historySession) {
        m_historySession = new GattHistorySession(this);
        connect(m_historySession, &GattHistorySession::busyChanged, this,
                &BleScanner::historySyncBusyChanged);
        // Forget the history target when the download ends, so "connected" (blue dot) clears.
        connect(m_historySession, &GattHistorySession::busyChanged, this, [this](bool busy) {
            if (!busy && !m_historyGattId.isEmpty()) {
                m_historyGattId.clear();
                Q_EMIT gattTargetChanged(activeGattId());
            }
            if (!busy)
                scheduleScanResume();
        });
        connect(m_historySession, &GattHistorySession::progress, this,
                &BleScanner::historySyncProgress);
        connect(m_historySession, &GattHistorySession::failed, this,
                &BleScanner::historySyncFailed);
        connect(m_historySession, &GattHistorySession::finished, this,
                &BleScanner::historySyncFinished);
    }

    qCDebug(lcBle) << "syncHistory" << id << "model=" << impl->model();
    pauseScanForGatt(); // BlueZ won't reliably connect while discovery is running
    m_historyGattId = id;
    Q_EMIT gattTargetChanged(activeGattId());
    m_historySession->sync(id, infoIt.value(), *profile, lastSyncMs, nowMs);
}

} // namespace klr
