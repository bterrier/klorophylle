// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "providerconfig.h"
#include "responsesdialect.h"
#include "streamingprovider.h"

#include <memory>

namespace karness {

// IProvider for the OpenAI Responses API (docs/adr/0019 decision 3b) — a thin
// StreamingProvider over ResponsesDialect. baseUrl is the API root (e.g.
// https://api.openai.com/v1); the dialect appends /responses and sets the
// Authorization: Bearer header.
class ResponsesProvider : public StreamingProvider {
    Q_OBJECT
public:
    explicit ResponsesProvider(ProviderConfig config, QObject *parent = nullptr)
        : StreamingProvider(std::move(config), std::make_unique<ResponsesDialect>(), parent)
    {
    }
};

} // namespace karness
