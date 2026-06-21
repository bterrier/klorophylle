// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "inferencerequest.h"

namespace karness {

// Maps the normalized effort knob to a thinking/reasoning token budget for the
// dialects that take one (Anthropic `thinking.budget_tokens`, Gemini
// `thinkingConfig.thinkingBudget`). Off -> 0, which the caller treats as "omit
// / disable thinking". OpenAI dialects use the string effort directly, not
// this. (docs/adr/0019 decision 6.)
inline int thinkingBudgetTokens(ReasoningEffort effort)
{
    switch (effort) {
    case ReasoningEffort::Off: return 0;
    case ReasoningEffort::Low: return 1024;
    case ReasoningEffort::Medium: return 4096;
    case ReasoningEffort::High: return 16384;
    }
    return 0;
}

} // namespace karness
