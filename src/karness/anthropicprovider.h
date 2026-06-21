// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "anthropicdialect.h"
#include "providerconfig.h"
#include "streamingprovider.h"

#include <memory>

namespace karness {

// IProvider for the Anthropic Messages API (docs/adr/0019 decision 3c) — a
// thin StreamingProvider over AnthropicDialect. baseUrl is the API root (e.g.
// https://api.anthropic.com/v1); the dialect appends /messages and sets the
// x-api-key / anthropic-version headers.
class AnthropicProvider : public StreamingProvider {
    Q_OBJECT
public:
    explicit AnthropicProvider(ProviderConfig config, QObject *parent = nullptr)
        : StreamingProvider(std::move(config), std::make_unique<AnthropicDialect>(), parent)
    {
    }
};

} // namespace karness
