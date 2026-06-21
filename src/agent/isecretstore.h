// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>

#include <optional>

namespace klr {

// The seam for secret storage — API keys for remote LLM providers (ADR 0019 decision 9:
// keys live in the freedesktop Secret Service, NEVER in QSettings). klr_agent owns the
// interface; the real D-Bus-backed implementation lives in the executable (exe-only Qt6::DBus,
// like the notification sink), and InMemorySecretStore stands in for tests + the Ollama-first
// path (local endpoints need no key). Keys are short, opaque strings (e.g. "agent/apiKey").
class ISecretStore {
public:
    virtual ~ISecretStore() = default;

    virtual void setSecret(const QString &key, const QString &value) = 0;
    [[nodiscard]] virtual std::optional<QString> secret(const QString &key) const = 0;
    virtual void removeSecret(const QString &key) = 0;
};

} // namespace klr
