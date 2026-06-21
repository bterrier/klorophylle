// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gatthistoryprofile.h" // klr_devices
#include "reading.h"            // klr_core

#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
class QLowEnergyController;
class QLowEnergyService;
class QTimer;
QT_END_NAMESPACE

namespace klr {

// Stateful GATT history downloader (ADR 0014), sibling to the one-shot GattSession: connect ->
// (optional MiBeacon RC4 handshake) -> enter history mode -> read the device clock -> read the entry
// count -> read back the newest entries since the last sync -> read battery -> disconnect. Driven by
// a GattHistoryProfile (the pure wire knowledge lives in klr_devices); the interactive
// challenge/response handshake is run here because it cannot be declarative. Hardware-verified — like
// GattSession there is no BLE in CI. One sync at a time.
//
// The caller passes the last completed sync time + "now" so the bounded window
// (historyEntriesToRead) is computed deterministically; on success it reports the readings, the
// opportunistic battery sample, and the time synced THROUGH (== nowMs, never derived from the last
// entry).
class GattHistorySession : public QObject {
    Q_OBJECT
public:
    explicit GattHistorySession(QObject *parent = nullptr);
    ~GattHistorySession() override;

    bool isBusy() const { return m_busy; }

    // Start a history download. `id` is echoed back. `lastSyncMs` is nullopt when never synced
    // (reads the whole buffer). No-op-with-failure if a sync is already running.
    void sync(const QString &id, const QBluetoothDeviceInfo &info, const GattHistoryProfile &profile,
              std::optional<qint64> lastSyncMs, qint64 nowMs);

signals:
    // `complete` is false when the download was cut short (timeout/disconnect) and only PARTIAL
    // entries were salvaged — the caller must then NOT advance the last-sync marker, so the backlog
    // is retried (battery, read up front, is still delivered).
    void finished(const QString &id, const std::vector<klr::Reading> &history,
                  const std::vector<klr::Reading> &battery, qint64 syncedThroughMs, bool complete);
    void failed(const QString &id, const QString &message);
    void progress(const QString &id, int done, int total);
    void busyChanged(bool busy);

private:
    enum class Step {
        Idle,
        HandshakeStart,   // wrote the start command, awaiting its write-confirm
        HandshakeChallenge, // wrote the challenge, awaiting its write-confirm
        HandshakeFinish,  // wrote the finish token, awaiting its write-confirm
        EnterMode,        // wrote the history-mode command, awaiting its write-confirm
        ReadDeviceTime,   // read the device clock
        ReadCount,        // read the entry count
        SelectEntry,      // wrote an entry address, awaiting its write-confirm
        ReadEntry,        // read one entry payload
        ReadBattery,      // read the battery characteristic
    };

    void onDiscoveryFinished();
    void onServiceDiscovered();
    void beginAfterDiscovery();
    void doHandshakeStart();
    void enterHistoryMode();
    void requestEntry(int index);
    void readBattery();
    // Begin reading the history backlog (after battery is read); succeeds immediately if nothing
    // is due.
    void startEntries();
    void onWritten(const QString &uuid);
    void onRead(const QString &uuid, const QByteArray &value);
    void succeed(bool complete = true);
    void fail(const QString &message);
    void cleanup();
    // Tear down a controller without tripping BlueZ's "deleted in ClosingState" warning: delete it
    // now if already unconnected, else close it and deleteLater once it has disconnected.
    static void deleteControllerWhenClosed(QLowEnergyController *c);
    QLowEnergyService *svc(const QString &uuid) const;

    bool m_busy = false;
    QString m_id;
    GattHistoryProfile m_profile;
    QByteArray m_mac;            // 6 bytes, natural order (for the handshake token)
    std::optional<qint64> m_lastSyncMs;
    qint64 m_nowMs = 0;
    int m_timeoutMs = 30000;     // watchdog window; restarted on every BLE response (see onRead/onWritten)

    QLowEnergyController *m_controller = nullptr;
    QHash<QString, QLowEnergyService *> m_services; // canonical service uuid -> object
    int m_servicesPending = 0;
    QTimer *m_timeout = nullptr;

    Step m_step = Step::Idle;
    qint64 m_wallEpochMs = 0;     // device boot wall-clock = now - uptime
    int m_count = 0;              // entries the device holds
    int m_toRead = 0;            // entries we decided to fetch (newest)
    int m_index = 0;             // current entry index (0 = newest)
    std::vector<Reading> m_history;
    std::vector<Reading> m_battery;
};

} // namespace klr
