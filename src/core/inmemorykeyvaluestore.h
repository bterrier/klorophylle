// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ikeyvaluestore.h"

#include <QtCore/QHash>

namespace klr {

// The in-memory fake used by tests (and a viable ephemeral fallback): a plain QHash, no
// I/O. Parity with the QSettings-backed impl is by the shared IKeyValueStore contract.
class InMemoryKeyValueStore final : public IKeyValueStore {
public:
    QVariant value(const QString &key, const QVariant &defaultValue = {}) const override
    {
        return m_map.value(key, defaultValue);
    }
    void setValue(const QString &key, const QVariant &value) override { m_map.insert(key, value); }

private:
    QHash<QString, QVariant> m_map;
};

} // namespace klr
