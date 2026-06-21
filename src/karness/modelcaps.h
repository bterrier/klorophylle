// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace karness {

// Capabilities a provider/model advertises; hosts gate features on these
// (e.g. a photo tool requires vision — docs/adr/0019 decision 5).
struct ModelCaps {
    bool toolCalling = false;
    bool vision = false;
    bool reasoning = false;
    int contextTokens = 0;

    bool operator==(const ModelCaps &) const = default;
};

} // namespace karness
