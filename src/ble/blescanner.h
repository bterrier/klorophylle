// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "discovereddevice.h"

#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <memory>
#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
class QBluetoothDeviceDiscoveryAgent;
QT_END_NAMESPACE

namespace klr {

class Device;
class DeviceRegistry;
class GattSession;
class GattHistorySession;

// Passive BLE advertisement listener. It NEVER opens a GATT connection, so it
// never drains a sensor's coin cell — advertisement-first monitoring (goal #4). GUI-free and QML-free, so the
// headless probe links the exact same class. Decoding is delegated to
// the pure decodeAdvertisement() in klr_core.
//
// Note (verified against the Qt 6.11 docs): the Win32 backend does not deliver
// advertisement data updates after discovery, so live values are unavailable on
// Windows via this path — another reason the headless probe matters there.
class BleScanner : public QObject {
    Q_OBJECT
public:
    // The device registry is injected (composition root owns it); the scanner uses
    // it to instantiate the matching Device subclass for each discovered device.
    explicit BleScanner(const DeviceRegistry &registry, QObject *parent = nullptr);
    ~BleScanner() override;

    bool isScanning() const;
    QList<DiscoveredDevice> devices() const;          // snapshot, discovery order
    const DiscoveredDevice *device(const QString &id) const;

    bool isReading() const;
    // The handle of the device a one-shot GATT read is currently open to, or "" when idle.
    // Lets a per-sensor status surface show a "connected" badge for the right row.
    QString currentGattId() const { return m_gattId; }
    // The handle of the device ANY GATT connection is currently open to — a one-shot read OR a
    // history download — or "" when the radio is idle. The single source of truth for "connected",
    // so every surface (incl. the plant cards, which can't see the history controller) renders the
    // connected device blue rather than offline while it is off the air.
    QString activeGattId() const { return m_gattId.isEmpty() ? m_historyGattId : m_gattId; }

public slots:
    void start();
    void stop();

    // One-shot GATT read of a non-broadcast device's current values: connect,
    // read, disconnect. The decoded values are merged into the device's latest
    // and surfaced via deviceChanged(id), exactly like an advertisement update.
    void readValue(const QString &id);

    // Download a device's on-device history log over GATT (ADR 0014): connect,
    // (handshake), read the entries newer than `lastSyncMs` plus battery, disconnect.
    // The caller (HistorySyncController) owns the cadence + persistence; results return
    // via historySync{Finished,Failed,Progress}. `lastSyncMs` nullopt = read all.
    void syncHistory(const QString &id, std::optional<qint64> lastSyncMs, qint64 nowMs);

signals:
    void scanningChanged(bool scanning);
    void deviceAdded(const QString &id);
    void deviceChanged(const QString &id);
    void errorOccurred(const QString &message);
    void readingChanged(bool reading);
    // The one-shot GATT read target changed (a connection opened to `id`, or "" when it closed).
    void gattTargetChanged(const QString &id);
    void readFailed(const QString &id, const QString &message);
    // History sync (ADR 0014). `history` are the downloaded log entries (Provenance::History),
    // `battery` the opportunistic battery sample, `syncedThroughMs` the time synced through (now).
    void historySyncProgress(const QString &id, int done, int total);
    // `complete` is false when only a partial backlog was salvaged (cut-short download): persist the
    // readings + battery but leave the last-sync marker untouched so the next sweep retries.
    void historySyncFinished(const QString &id, const std::vector<Reading> &history,
                             const std::vector<Reading> &battery, qint64 syncedThroughMs,
                             bool complete);
    void historySyncFailed(const QString &id, const QString &message);
    void historySyncBusyChanged(bool busy);

private:
    void startNow();
    void ingest(const QBluetoothDeviceInfo &info);
    void mergeReadings(const QString &id, const std::vector<Reading> &readings);

    const DeviceRegistry &m_registry;
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    GattSession *m_gatt = nullptr;                     // one-shot GATT reader (lazy)
    GattHistorySession *m_historySession = nullptr;    // history downloader (lazy)
    QHash<QString, DiscoveredDevice> m_devices;
    QHash<QString, QBluetoothDeviceInfo> m_infos;      // raw infos, needed to open a GATT connection
    QList<QString> m_order;                            // discovery order; mirrors the list-model rows
    QHash<QString, Device *> m_impls;                  // matched Device subclass per id (non-owning)
    std::vector<std::unique_ptr<Device>> m_ownedImpls; // owns the device instances
    QString m_gattId;                                  // device the one-shot read is open to ("" idle)
    QString m_historyGattId;                           // device the history download is open to ("" idle)

    // BlueZ can't reliably open a GATT connection while LE discovery is running (the 2nd+ connect
    // fails with UnknownRemoteDeviceError and the adapter can wedge), so we stop scanning around a
    // session and resume after. Set only when WE paused an active scan (never forces scan on if the
    // user had it off).
    bool m_scanPausedForGatt = false;
    void pauseScanForGatt();   // stop discovery before a GATT session, remembering to resume
    void scheduleScanResume(); // resume discovery shortly after the last session ends (debounced)
};

} // namespace klr
