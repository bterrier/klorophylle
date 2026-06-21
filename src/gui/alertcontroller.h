// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "carestatus.h" // CareStatus
#include "reading.h"    // Quantity
#include "uiformat.h"   // Format (notification text)

#include <QtCore/QHash>
#include <QtCore/QPair>
#include <QtCore/QString>

namespace klr {

class IPlantRepository;
class IBindingRepository;
class IReadingRepository;
class ICareThresholdRepository;
class ICatalogRepository;
class Clock;
class SettingsStore;
class INotificationSink;

// Fires a user notification when a plant's care status CROSSES into an out-of-range state
// (ADR 0016). Reading-driven: AppContext calls evaluate() after the always-on ingester
// stores a fresh sample. The judgment reuses evaluatePlantCare (shared with the list
// health pill); this class adds the TRANSITION debounce — it remembers the last status per
// (plant, quantity) and notifies only on a not-alerting → alerting edge (shouldNotify), so a
// plant that stays dry across many advertisements is announced once, not once per sample.
//
// Global enable + a snooze deadline (SettingsStore) gate DELIVERY only: the previous-status
// memory is primed even while muted, so un-snoozing / re-enabling never replays a transition
// the user chose to skip. The memory is in-memory: a restart re-evaluates from scratch, so a
// still-dry plant re-announces on the first advertisement after launch (acceptable for an
// always-on monitor — that IS "this plant still needs water"; persistence is a follow-up).
// Delivery goes through INotificationSink (the freedesktop backend lives in app/).
class AlertController {
public:
    AlertController(IPlantRepository &plants, IBindingRepository &bindings,
                    IReadingRepository &readings, ICareThresholdRepository &thresholds,
                    const ICatalogRepository *catalog, const Clock &clock,
                    const SettingsStore &settings, INotificationSink &sink);

    // Re-judge every plant at the current clock time and notify on fresh transitions. Cheap
    // enough to run on each ingest — it mirrors the plant-list refresh whose logic it shares.
    void evaluate();

private:
    IPlantRepository &m_plants;
    IBindingRepository &m_bindings;
    IReadingRepository &m_readings;
    ICareThresholdRepository &m_thresholds;
    const ICatalogRepository *m_catalog;
    const Clock &m_clock;
    const SettingsStore &m_settings;
    INotificationSink &m_sink;
    Format m_format; // pure enum→text mapping (klr_style); stateless
    // (plant id, quantity) → last seen status, the per-edge debounce memory.
    QHash<QPair<QString, int>, CareStatus> m_previous;
};

} // namespace klr
