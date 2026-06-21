// SPDX-License-Identifier: GPL-3.0-or-later
#include "alertcontroller.h"

#include "careevaluation.h"
#include "clock.h"
#include "inotificationsink.h"
#include "iplantrepository.h"
#include "plant.h"
#include "settingsstore.h"

#include <QtCore/QDateTime>
#include <QtCore/QTimeZone>

namespace klr {

AlertController::AlertController(IPlantRepository &plants, IBindingRepository &bindings,
                                IReadingRepository &readings,
                                ICareThresholdRepository &thresholds,
                                const ICatalogRepository *catalog, const Clock &clock,
                                const SettingsStore &settings, INotificationSink &sink)
    : m_plants(plants)
    , m_bindings(bindings)
    , m_readings(readings)
    , m_thresholds(thresholds)
    , m_catalog(catalog)
    , m_clock(clock)
    , m_settings(settings)
    , m_sink(sink)
{
}

void AlertController::evaluate()
{
    const qint64 nowMs = m_clock.nowMs();
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC);
    // Delivery is gated by the global switch + the snooze deadline; the previous-status memory
    // is updated regardless, so a transition skipped while muted is not replayed on un-mute.
    const bool deliver =
        m_settings.notificationsEnabled() && nowMs >= m_settings.notificationsSnoozedUntilMs();

    for (const Plant &p : m_plants.all()) {
        const PlantCareSnapshot snap =
            evaluatePlantCare(p, m_bindings, m_readings, m_thresholds, m_catalog, now);
        // statuses is parallel to current when there are ranges to judge, and empty otherwise
        // (nothing to alert on) — so iterating statuses is always in-bounds for current.
        for (int i = 0; i < snap.statuses.size(); ++i) {
            const Quantity q = snap.current.at(i).quantity;
            const CareStatus status = snap.statuses.at(i);
            const QPair<QString, int> key(p.id.toString(), int(q));
            const CareStatus previous = m_previous.value(key, CareStatus::Unknown);
            m_previous.insert(key, status);
            if (deliver && shouldNotify(previous, status))
                m_sink.notify(m_format.notificationTitle(p.displayName),
                              m_format.notificationBody(int(q), int(status)));
        }
    }
}

} // namespace klr
