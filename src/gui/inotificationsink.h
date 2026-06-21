// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>

namespace klr {

// The platform-delivery seam for user notifications. The AlertController decides WHEN
// and WHAT to notify (pure rule, clock-injected, tested against a recording fake); this
// interface is the thin "show it" boundary, so the desktop backend (freedesktop DBus, which
// lives in app/ to keep this library DBus-free) is swappable for the per-platform backends
// that arrive with the mobile build. Fire-and-forget: delivery failures are the backend's own concern.
class INotificationSink {
public:
    virtual ~INotificationSink() = default;

    // Deliver one user-facing notification: `title` the headline (plant name / summary),
    // `body` the detail line.
    virtual void notify(const QString &title, const QString &body) = 0;
};

} // namespace klr
