// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "providerconfig.h"
#include "streamingprovider.h"

namespace karness {

// Config alias kept for the OpenAI-compatible provider's call sites; every
// dialect now shares ProviderConfig (baseUrl + apiKey + caps + stallTimeout).
using OpenAiCompatConfig = ProviderConfig;

// IProvider over any OpenAI-Chat-Completions-compatible endpoint — Ollama,
// llama.cpp server, LM Studio, vLLM, OpenRouter, BYOK OpenAI… (ADR 0019
// decisions 3a/4). A thin StreamingProvider over ChatCompletionsDialect: the
// shared transport owns sockets and the streaming contract, the dialect owns
// the wire format.
class OpenAiCompatProvider : public StreamingProvider {
    Q_OBJECT
public:
    explicit OpenAiCompatProvider(OpenAiCompatConfig config, QObject *parent = nullptr);
};

} // namespace karness
