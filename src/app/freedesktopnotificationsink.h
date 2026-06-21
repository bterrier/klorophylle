// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "inotificationsink.h"

namespace klr {

// The desktop-Linux notification backend: delivers via the freedesktop
// org.freedesktop.Notifications D-Bus service (the session bus), so no Qt Widgets / tray
// dependency and the app stays a QGuiApplication. Fire-and-forget (async, reply ignored);
// if no notification server is running the call simply does nothing. Lives in app/ so only
// the executable links Qt6::DBus — klr_gui stays platform- and DBus-free. Per-platform
// backends (and a possible tray) arrive with the mobile build; this is the one concrete sink for now.
class FreedesktopNotificationSink final : public INotificationSink {
public:
    void notify(const QString &title, const QString &body) override;
};

} // namespace klr
