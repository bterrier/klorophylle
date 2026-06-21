// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "isecretstore.h"

#include <QtDBus/QDBusObjectPath>

namespace klr {

// ISecretStore over the freedesktop Secret Service (org.freedesktop.secrets, D-Bus) — where
// remote-provider API keys belong (ADR 0019 decision 9, never QSettings). Lives in the executable
// and links Qt6::DBus exactly like FreedesktopNotificationSink, so no library gains a platform
// dependency. Best-effort and fail-soft: any D-Bus error degrades to "no secret" / no-op, so the
// app (and the keyless local-Ollama path) keeps working when no keyring is available.
//
// Items are stored in the default collection, keyed by attributes {application, key}, with a
// "plain" transfer session (the value crosses the local session bus unencrypted — acceptable for
// a desktop app; the keyring encrypts at rest).
class FreedesktopSecretStore final : public ISecretStore {
public:
    FreedesktopSecretStore();

    void setSecret(const QString &key, const QString &value) override;
    [[nodiscard]] std::optional<QString> secret(const QString &key) const override;
    void removeSecret(const QString &key) override;

private:
    // Open (once) a plain transfer session; returns false if the service is unavailable.
    bool ensureSession() const;

    mutable QDBusObjectPath m_session; // valid path once a session is open
};

} // namespace klr
