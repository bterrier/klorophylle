// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "modelcaps.h"

#include <QtCore/QString>
#include <QtCore/QUrl>

#include <chrono>

namespace karness {

// Transport config shared by every SSE-based provider dialect (the OpenAI
// Chat Completions / Responses, Anthropic Messages and Gemini providers all
// take the same shape — docs/adr/0019 decision 3). baseUrl is the
// host-configured endpoint root; the dialect appends its path. apiKey empty
// means no auth header (a local Ollama / llama-server needs none). caps are
// host-declared (no capability probing).
struct ProviderConfig {
    QUrl baseUrl;
    QString apiKey;
    ModelCaps caps;

    // Transport stall guard: QNetworkRequest's transfer timeout resets on
    // every byte, so a token-by-token stream never trips it — only a silent
    // peer does. The wall-clock per-turn budget is the AgentSession, not the
    // provider. 0 disables.
    std::chrono::milliseconds stallTimeout = std::chrono::seconds(120);
};

} // namespace karness
