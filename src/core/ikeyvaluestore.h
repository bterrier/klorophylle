// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>
#include <QtCore/QVariant>

namespace klr {

// A tiny typed-by-convention key/value persistence seam for device-local UI preferences
// (see docs/adr/0008). Injected at the composition root — no getInstance(). Kept in
// klr_core as a pure interface so klr_style's SettingsStore can depend on it without
// reaching into the storage layer; the concrete QSettings-backed impl lives in
// klr_persistence, the in-memory fake (below) drives the tests. NOT for domain data —
// plants/sensors/readings stay in the sync-ready SQLite schema.
class IKeyValueStore {
public:
    virtual ~IKeyValueStore() = default;

    virtual QVariant value(const QString &key, const QVariant &defaultValue = {}) const = 0;
    virtual void setValue(const QString &key, const QVariant &value) = 0;
};

} // namespace klr
