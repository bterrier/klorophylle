// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <vector>

namespace klr {

class BleScanner;
class ISensorRepository;
class IReadingRepository;
class ISyncStateRepository;
class SettingsStore;
class Clock;
struct Reading;

// Owns the WHEN of GATT history backfill (ADR 0014, slice C). On a startup grace delay and a
// periodic timer it builds the due-list — discovered, registered, history-capable sensors whose last
// sync is older than the cadence (pure isHistorySyncDue) — and drives ONE BleScanner::syncHistory at
// a time (single BLE radio). On completion it appends the downloaded entries (Provenance::History) +
// the opportunistic battery sample through the reading repository and stamps last-sync = now (never
// the last entry's time), then asks the UI to refresh via changed(). Cadence + on/off come from
// SettingsStore; a manual sync ignores the cadence. Non-QML — AppContext drives + surfaces it.
class HistorySyncController : public QObject {
    Q_OBJECT
public:
    HistorySyncController(BleScanner &scanner, ISensorRepository &sensors, IReadingRepository &readings,
                          ISyncStateRepository &syncState, const Clock &clock,
                          const SettingsStore &settings, QObject *parent = nullptr);

    // Begin auto-sync: a one-shot startup-grace sweep, then a periodic sweep. Safe to call once,
    // from the composition root after the engine is up.
    void start();

    bool isBusy() const { return m_active; }
    // The handle of the sensor a history download is currently open to, or "" when idle —
    // so a per-sensor status surface can show a "connected" badge on the right row.
    QString activeId() const { return m_activeId; }

public slots:
    // Sweep now, honouring the cadence (the periodic/auto path).
    void sweep();
    // Sync every discovered, registered, history-capable sensor NOW, ignoring the cadence (the
    // manual "Sync history" action).
    void syncNow();

signals:
    void busyChanged(bool busy);
    void started(const QString &id);
    void finished(const QString &id, int entries);
    void failed(const QString &id, const QString &message);
    // A sync persisted new readings (or refreshed battery) — the UI should refresh.
    void changed();

private:
    void enqueueDue(bool ignoreCadence);
    void processNext();
    void onFinished(const QString &id, const std::vector<Reading> &history,
                    const std::vector<Reading> &battery, qint64 syncedThroughMs, bool complete);
    void onFailed(const QString &id, const QString &message);

    BleScanner &m_scanner;
    ISensorRepository &m_sensors;
    IReadingRepository &m_readings;
    ISyncStateRepository &m_syncState;
    const Clock &m_clock;
    const SettingsStore &m_settings;

    QList<QString> m_queue;   // discovered handle ids awaiting sync
    bool m_active = false;     // a sync is in flight
    QString m_activeId;
};

} // namespace klr
