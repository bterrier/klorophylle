// SPDX-License-Identifier: GPL-3.0-or-later
// Layer seam for the AI agent integration (docs/adr/0019). Real sources land
// with the ContextBuilder / domain tools and the AgentRepository; this TU pins
// klr_agent's graph position — karness + klr_core + klr_persistence ->
// klr_agent — until then. The includes below are the wiring proof.
#include "iprovider.h"        // karness
#include "iplantrepository.h" // klr_persistence

namespace klr {
} // namespace klr
