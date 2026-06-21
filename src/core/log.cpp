// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.h"

namespace klr {
// QtWarningMsg default level => qCDebug is OFF by default (re-enable with
// QT_LOGGING_RULES="klr.*.debug=true"); qCWarning/qCCritical always show.
Q_LOGGING_CATEGORY(lcApp, "klr.app", QtWarningMsg)
Q_LOGGING_CATEGORY(lcBle, "klr.ble", QtWarningMsg)
}
