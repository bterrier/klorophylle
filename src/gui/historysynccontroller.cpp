// SPDX-License-Identifier: GPL-3.0-or-later
#include "historysynccontroller.h"

#include "blescanner.h"
#include "clock.h"
#include "discovereddevice.h"
#include "historysync.h"  // klr_core: isHistorySyncDue
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "isyncstaterepository.h"
#include "log.h"
#include "reading.h"
#include "sensor.h"       // HandleKind
#include "settingsstore.h"
#include "storageerror.h"

#include <QtCore/QDateTime>
#include <QtCore/QTimeZone>
#include <QtCore/QTimer>

#include <span>

namespace klr {

namespace {
// Let advertisements/discovery populate before the first connect (and don't fight startup).
constexpr int kStartupGraceMs = 20'000;
// Re-check the due-list this often; the cadence gate decides whether to actually connect.
constexpr int kSweepIntervalMs = 30 * 60 * 1000;
// After a device is discovered, wait this long (coalescing a burst of discoveries) before
// sweeping — so launch backfill starts as soon as paired sensors appear, not on the 30-min timer.
constexpr int kDiscoverySweepDebounceMs = 5'000;
} // namespace

HistorySyncController::HistorySyncController(BleScanner &scanner, ISensorRepository &sensors,
                                             IReadingRepository &readings,
                                             ISyncStateRepository &syncState, const Clock &clock,
                                             const SettingsStore &settings, QObject *parent)
    : QObject(parent)
    , m_scanner(scanner)
    , m_sensors(sensors)
    , m_readings(readings)
    , m_syncState(syncState)
    , m_clock(clock)
    , m_settings(settings)
{
    connect(&m_scanner, &BleScanner::historySyncFinished, this, &HistorySyncController::onFinished);
    connect(&m_scanner, &BleScanner::historySyncFailed, this, &HistorySyncController::onFailed);
}

void HistorySyncController::start()
{
    QTimer::singleShot(kStartupGraceMs, this, &HistorySyncController::sweep);
    auto *timer = new QTimer(this);
    timer->setInterval(kSweepIntervalMs);
    connect(timer, &QTimer::timeout, this, &HistorySyncController::sweep);
    timer->start();

    // Sweep shortly after new devices are discovered (debounced), so backfill begins as soon as
    // paired sensors appear at launch — without waiting out the periodic interval or needing the
    // user to open the Sensors screen. The cadence gate still decides whether to actually connect.
    auto *discoverySweep = new QTimer(this);
    discoverySweep->setSingleShot(true);
    discoverySweep->setInterval(kDiscoverySweepDebounceMs);
    connect(discoverySweep, &QTimer::timeout, this, &HistorySyncController::sweep);
    connect(&m_scanner, &BleScanner::deviceAdded, discoverySweep,
            qOverload<>(&QTimer::start)); // coalesce a burst of discoveries into one sweep
}

void HistorySyncController::sweep()
{
    if (!m_settings.historySyncEnabled())
        return;
    enqueueDue(/*ignoreCadence*/ false);
    processNext();
}

void HistorySyncController::syncNow()
{
    enqueueDue(/*ignoreCadence*/ true);
    processNext();
}

void HistorySyncController::enqueueDue(bool ignoreCadence)
{
    const qint64 now = m_clock.nowMs();
    const qint64 cadenceMs = qint64(m_settings.historySyncIntervalHours()) * 3'600'000LL;
    for (const DiscoveredDevice &d : m_scanner.devices()) {
        if (!d.canSyncHistory)
            continue; // no on-device history log
        const std::optional<Sensor> sensor = m_sensors.findByHandle(HandleKind::Mac, d.id);
        if (!sensor)
            continue; // not a paired/registered sensor — never persist for unknown devices
        if (!ignoreCadence) {
            const std::optional<QDateTime> last = m_syncState.lastHistorySync(sensor->id);
            const std::optional<qint64> lastMs =
                last ? std::optional<qint64>(last->toMSecsSinceEpoch()) : std::nullopt;
            if (!isHistorySyncDue(lastMs, now, cadenceMs))
                continue;
        }
        if (!m_queue.contains(d.id))
            m_queue.append(d.id);
    }
}

void HistorySyncController::processNext()
{
    if (m_active || m_queue.isEmpty())
        return;
    const QString id = m_queue.takeFirst();
    const std::optional<Sensor> sensor = m_sensors.findByHandle(HandleKind::Mac, id);
    if (!sensor) { // de-registered since enqueue — skip
        processNext();
        return;
    }
    const std::optional<QDateTime> last = m_syncState.lastHistorySync(sensor->id);
    const std::optional<qint64> lastMs =
        last ? std::optional<qint64>(last->toMSecsSinceEpoch()) : std::nullopt;

    m_active = true;
    m_activeId = id;
    Q_EMIT busyChanged(true);
    Q_EMIT started(id);
    m_scanner.syncHistory(id, lastMs, m_clock.nowMs());
}

void HistorySyncController::onFinished(const QString &id, const std::vector<Reading> &history,
                                       const std::vector<Reading> &battery, qint64 syncedThroughMs,
                                       bool complete)
{
    if (id != m_activeId)
        return;
    const std::optional<Sensor> sensor = m_sensors.findByHandle(HandleKind::Mac, id);
    if (sensor) {
        try {
            if (!history.empty())
                m_readings.append(sensor->id, std::span<const Reading>(history.data(), history.size()));
            if (!battery.empty())
                m_readings.append(sensor->id, std::span<const Reading>(battery.data(), battery.size()));
            // Only advance the marker on a FULL download. Entries are read newest-first, so a
            // cut-short read (complete=false) keeps the newest but misses older-yet-still-newer
            // entries; leaving last-sync untouched makes the next sweep re-fetch them. Stamp the
            // COMPLETION time (now), never the last entry's time (advertisement-monitoring.md).
            if (complete)
                m_syncState.setLastHistorySync(
                    sensor->id, QDateTime::fromMSecsSinceEpoch(syncedThroughMs, QTimeZone::UTC));
        } catch (const StorageError &e) {
            qCWarning(lcApp) << "history sync persist failed:" << e.what();
        }
        Q_EMIT finished(id, int(history.size()));
        Q_EMIT changed(); // refresh plant list / open care view
    }
    m_active = false;
    m_activeId.clear();
    Q_EMIT busyChanged(false);
    QTimer::singleShot(0, this, &HistorySyncController::processNext); // queued: avoid re-entrancy
}

void HistorySyncController::onFailed(const QString &id, const QString &message)
{
    if (id != m_activeId)
        return;
    qCWarning(lcApp) << "history sync failed for" << id << ":" << message;
    // Do NOT update last-sync, so the sensor is retried on the next sweep.
    m_active = false;
    m_activeId.clear();
    Q_EMIT busyChanged(false);
    Q_EMIT failed(id, message);
    QTimer::singleShot(0, this, &HistorySyncController::processNext);
}

} // namespace klr
