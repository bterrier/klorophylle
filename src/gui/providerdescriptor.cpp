// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerdescriptor.h"

#include <algorithm>

namespace klr {

const ProviderDescriptor &providerDescriptor(int type)
{
    // Order matches SettingsStore::agentProviderType / the AgentViewModel provider factory.
    static const ProviderDescriptor table[] = {
        // 0 — OpenAI-compatible (Chat Completions): the BYO-endpoint branch covering Ollama,
        // llama.cpp, vLLM, OpenRouter and a remote OpenAI. No fixed endpoint (user supplies it),
        // key optional (local servers need none). The text-only seed is the common local set —
        // including the default model — so turning vision on with stock Ollama is gently flagged.
        ProviderDescriptor{
            QStringLiteral("OpenAI-compatible"),
            QString(), // user-configured (defaults to the local Ollama URL)
            false,
            QString(),
            QStringLiteral("qwen2.5"),
            { QStringLiteral("qwen2.5"), QStringLiteral("qwen2.5-vl"), QStringLiteral("llama3.1"),
              QStringLiteral("llama3.2-vision"), QStringLiteral("mistral"),
              QStringLiteral("gpt-4o"), QStringLiteral("gpt-4o-mini") },
            { QStringLiteral("qwen2.5"), QStringLiteral("llama3.1"), QStringLiteral("mistral") },
            QString(),
        },
        // 1 — OpenAI Responses.
        ProviderDescriptor{
            QStringLiteral("OpenAI Responses"),
            QStringLiteral("https://api.openai.com/v1"),
            true,
            QStringLiteral("https://platform.openai.com/api-keys"),
            QStringLiteral("gpt-4o-mini"),
            { QStringLiteral("gpt-4o"), QStringLiteral("gpt-4o-mini"), QStringLiteral("o4-mini"),
              QStringLiteral("o3") },
            {}, // the bundled OpenAI models are all vision-capable
            QString(),
        },
        // 2 — Anthropic Messages.
        ProviderDescriptor{
            QStringLiteral("Anthropic"),
            QStringLiteral("https://api.anthropic.com/v1"),
            true,
            QStringLiteral("https://console.anthropic.com/settings/keys"),
            QStringLiteral("claude-haiku-4-5"),
            { QStringLiteral("claude-sonnet-4-5"), QStringLiteral("claude-haiku-4-5"),
              QStringLiteral("claude-opus-4-1") },
            {}, // the bundled Claude models are all vision-capable
            QString(),
        },
        // 3 — Gemini (the free-tier hero): native dialect, free key from AI Studio, tools + vision.
        ProviderDescriptor{
            QStringLiteral("Gemini"),
            QStringLiteral("https://generativelanguage.googleapis.com/v1beta"),
            true,
            QStringLiteral("https://aistudio.google.com/apikey"),
            QStringLiteral("gemini-2.5-flash"),
            { QStringLiteral("gemini-2.5-flash"), QStringLiteral("gemini-2.5-pro"),
              QStringLiteral("gemini-2.0-flash") },
            {}, // the bundled Gemini models are all multimodal
            QStringLiteral("https://ai.google.dev/gemini-api/docs/rate-limits"),
        },
    };
    constexpr int count = int(std::size(table));
    if (type < 0 || type >= count)
        type = 0; // clamp to the OpenAI-compatible default, like the factory's default: branch
    return table[type];
}

} // namespace klr
