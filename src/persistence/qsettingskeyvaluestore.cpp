// SPDX-License-Identifier: GPL-3.0-or-later
#include "qsettingskeyvaluestore.h"

namespace klr {

QVariant QSettingsKeyValueStore::value(const QString &key, const QVariant &defaultValue) const
{
    return m_settings.value(key, defaultValue);
}

void QSettingsKeyValueStore::setValue(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
}

} // namespace klr
