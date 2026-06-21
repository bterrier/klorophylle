// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QLoggingCategory>

// Logging categories. Debug output is OFF by default; enable selectively, e.g.
//   QT_LOGGING_RULES="klr.*.debug=true"
// Warnings (qCWarning) are always shown. A single home for categories also gives
// the future headless probe a place to hook structured logging
// (observability).
namespace klr {
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcBle)
}
