// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace klr {

// Per-provider onboarding metadata, keyed by the SettingsStore::agentProviderType index
// (0 = OpenAI-compatible, 1 = OpenAI Responses, 2 = Anthropic, 3 = Gemini — the provider
// factory's branch order, ADR 0027). The single source of truth the AI settings screen reads
// from: a wrong endpoint or key URL is caught by test_providerdescriptor rather than shipped.
// Pure data — no Qt registration; AgentViewModel bridges it to QML as a QVariantMap.
struct ProviderDescriptor {
    // Shown in the provider combo.
    QString displayName;
    // The well-known base URL for a single-host cloud provider; the dialect appends its fixed
    // path suffix (/messages, /responses, /models/{model}:…). EMPTY for the OpenAI-compatible
    // branch only — the BYO-endpoint escape hatch, where the user supplies the endpoint.
    QString fixedEndpoint;
    // The cloud providers require a key; OpenAI-compatible may be keyless (local) — false there.
    bool needsKey = false;
    // "Get an API key" link; empty => no link (the OpenAI-compatible branch).
    QString keyUrl;
    // Seeds the model field when a preset selects this provider.
    QString defaultModel;
    // Autocomplete suggestions. A bundled static seed (live-fetch is a follow-up, ADR 0027);
    // free text is always accepted, so a stale entry never blocks the user.
    QStringList knownModels;
    // Models known to be text-only — drives the conservative capability warning (vision on +
    // text-only model). Unknown/free-text models are never flagged (no false positives).
    QStringList textOnlyModels;
    // The provider's own rate-limit / free-tier page; links out instead of hardcoding shifting
    // numbers. Empty => no free-tier note.
    QString freeTierUrl;
};

// The descriptor for a provider-type index. An out-of-range index clamps to the OpenAI-compatible
// default, mirroring the provider factory's `default:` branch.
const ProviderDescriptor &providerDescriptor(int type);

} // namespace klr
