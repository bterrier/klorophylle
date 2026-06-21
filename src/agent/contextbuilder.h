// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>

// Builds the deterministic "context block" that primes the agent each turn and is shown
// verbatim to the user in the pre-send disclosure (docs/adr/0019 decision 9). Its scope is
// ROSTER ONLY: the plant list + a count, no readings or care status. The
// agent reaches for the read_plant_data / read_plant_journal tools when it needs detail —
// this keeps the always-included prompt small and the data-exposure surface minimal.
//
// Pure and CLOCK-FREE: it reads the plant repository and sorts deterministically, so build()
// is byte-stable for a given repository (a hard requirement). No clock-derived values
// enter the block — it flows through AgentSession::setAmbient at the turn tail, and a
// clock value in a cacheable prefix is the anti-pattern (docs/adr/0019 cache-placement
// follow-up). Lives in klr_agent (karness + klr_core + klr_persistence), never klr_gui.
namespace klr {

class IPlantRepository;

class ContextBuilder {
public:
    explicit ContextBuilder(const IPlantRepository &plants);

    [[nodiscard]] QString build() const;

private:
    const IPlantRepository &m_plants;
};

} // namespace klr
