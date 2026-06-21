// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <functional>
#include <memory>
#include <vector>

namespace klr {

// Maps a discovered advertisement to the matching Device subclass (one registry,
// consulted by both live discovery and, later, DB-restore — fixes the two
// drifting switches). Order matters: specific models
// are registered before generic fallbacks.
class DeviceRegistry {
public:
    using Factory = std::function<std::unique_ptr<Device>()>;
    using Matcher = std::function<bool(const AdvertisementContext &)>;

    void add(Matcher matcher, Factory factory);

    // The first registered device whose matcher accepts ctx, or nullptr.
    std::unique_ptr<Device> create(const AdvertisementContext &ctx) const;

private:
    struct Entry {
        Matcher matcher;
        Factory factory;
    };
    std::vector<Entry> m_entries;
};

// A registry pre-populated with every supported device (the one place that lists
// them). Adding a device = a new device_*.h + one line here.
DeviceRegistry makeBuiltinRegistry();

} // namespace klr
