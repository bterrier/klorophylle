// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "isecretstore.h"

#include <QtCore/QHash>

namespace klr {

// In-process secret store — the test double and the Ollama-first stand-in until the real
// freedesktop Secret Service implementation lands. Holds secrets in memory
// only, so they do NOT persist across restarts (a keyless local endpoint is unaffected).
class InMemorySecretStore final : public ISecretStore {
public:
    void setSecret(const QString &key, const QString &value) override { m_secrets.insert(key, value); }

    std::optional<QString> secret(const QString &key) const override
    {
        const auto it = m_secrets.constFind(key);
        return it == m_secrets.constEnd() ? std::nullopt : std::optional<QString>(*it);
    }

    void removeSecret(const QString &key) override { m_secrets.remove(key); }

private:
    QHash<QString, QString> m_secrets;
};

} // namespace klr
