// SPDX-License-Identifier: GPL-3.0-or-later
#include "freedesktopnotificationsink.h"

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusPendingCall>

#include <QtCore/QStringList>
#include <QtCore/QVariantMap>

namespace klr {

void FreedesktopNotificationSink::notify(const QString &title, const QString &body)
{
    // org.freedesktop.Notifications.Notify(app_name, replaces_id, app_icon, summary, body,
    // actions, hints, expire_timeout). We build the message by hand (no QDBusInterface
    // introspection round-trip) and asyncCall it — fire-and-forget, the returned id is unused.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    msg << QStringLiteral("Klorophylle")          // app_name
        << quint32(0)                             // replaces_id (0 = a new notification)
        << QStringLiteral("dialog-information")   // app_icon (freedesktop icon name)
        << title                                  // summary
        << body                                   // body
        << QStringList()                          // actions (none)
        << QVariantMap()                          // hints (none)
        << qint32(-1);                            // expire_timeout (-1 = server default)
    QDBusConnection::sessionBus().asyncCall(msg);
}

} // namespace klr
