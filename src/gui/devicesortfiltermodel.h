// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QSortFilterProxyModel>

namespace klr {

class ISensorRepository;

// Orders the discovered-devices list so supported sensors surface above background
// BLE noise: supported-first, then strongest signal first. Optionally filters to
// supported-only (the sensor-pairing picker wants that; the browse list keeps the
// rest, dimmed, under an "Other devices" section). Optionally also drops devices that
// are ALREADY registered (`excludeRegistered` + a sensor repo) — the live-scan list
// then shows only NEW devices, since registered ones appear in the Sensors screen's
// "Registered sensors" section instead (no double-listing). Pure view ordering — no
// data of its own; the underlying DiscoveredDevicesModel stays in discovery order.
class DeviceSortFilterModel final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    // onlySupported: drop unrecognised devices entirely (for the pairing picker).
    // excludeRegistered + sensors: drop devices already in the `sensors` table (browse list).
    explicit DeviceSortFilterModel(bool onlySupported, bool excludeRegistered = false,
                                   ISensorRepository *sensors = nullptr, QObject *parent = nullptr);

    // Re-run the filter (the registered set changed — a sensor was paired / imported /
    // deleted). Driven from the composition root off the registered-sensors model's reset.
    Q_INVOKABLE void refilter();

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool m_onlySupported;
    bool m_excludeRegistered;
    ISensorRepository *m_sensors;
};

} // namespace klr
