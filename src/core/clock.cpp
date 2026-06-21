// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock.h"

#include <QtCore/QDateTime>

namespace klr {

qint64 SystemClock::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace klr
