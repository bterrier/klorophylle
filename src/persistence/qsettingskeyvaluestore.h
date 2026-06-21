// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ikeyvaluestore.h"

#include <QtCore/QSettings>

namespace klr {

// IKeyValueStore backed by QSettings (the platform-native preference store). Lives in
// klr_persistence — the storage layer — and is constructed at the composition root and
// injected upward as an IKeyValueStore. Uses the application/organization names set on
// QCoreApplication, so no path is hardcoded.
class QSettingsKeyValueStore final : public IKeyValueStore {
public:
    QVariant value(const QString &key, const QVariant &defaultValue = {}) const override;
    void setValue(const QString &key, const QVariant &value) override;

private:
    mutable QSettings m_settings;
};

} // namespace klr
