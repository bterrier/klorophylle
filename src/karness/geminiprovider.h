// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "geminidialect.h"
#include "providerconfig.h"
#include "streamingprovider.h"

#include <memory>

namespace karness {

// IProvider for the Gemini generateContent API (docs/adr/0019 decision 3d) — a
// thin StreamingProvider over GeminiDialect. baseUrl is the API root (e.g.
// https://generativelanguage.googleapis.com/v1beta); the dialect appends
// /models/{model}:streamGenerateContent?alt=sse and sets x-goog-api-key.
class GeminiProvider : public StreamingProvider {
    Q_OBJECT
public:
    explicit GeminiProvider(ProviderConfig config, QObject *parent = nullptr)
        : StreamingProvider(std::move(config), std::make_unique<GeminiDialect>(), parent)
    {
    }
};

} // namespace karness
