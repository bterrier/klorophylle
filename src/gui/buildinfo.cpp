// SPDX-License-Identifier: GPL-3.0-or-later
#include "buildinfo.h"

#include <klr/version.h>

namespace klr {

QString BuildInfo::appName() const
{
    return QStringLiteral("Klorophylle");
}

QString BuildInfo::appVersion() const
{
    return QStringLiteral(KLR_VERSION_STR);
}

QString BuildInfo::qtVersion() const
{
    return QString::fromLatin1(qVersion());
}

QString BuildInfo::buildType() const
{
#ifdef QT_DEBUG
    return QStringLiteral("Debug");
#else
    return QStringLiteral("Release");
#endif
}

QString BuildInfo::license() const
{
    return QStringLiteral("GPL-3.0-or-later");
}

QString BuildInfo::sourceUrl() const
{
    return QStringLiteral("https://github.com/bterrier/klorophylle");
}

} // namespace klr
