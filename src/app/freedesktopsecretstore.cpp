// SPDX-License-Identifier: GPL-3.0-or-later
#include "freedesktopsecretstore.h"

#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusMetaType>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusVariant>

#include <QtCore/QMap>
#include <QtCore/QVariantMap>

namespace {

// org.freedesktop.Secret.Service vocabulary.
constexpr auto kService = "org.freedesktop.secrets";
constexpr auto kServicePath = "/org/freedesktop/secrets";
constexpr auto kDefaultCollection = "/org/freedesktop/secrets/aliases/default";
constexpr auto kIfaceService = "org.freedesktop.Secret.Service";
constexpr auto kIfaceCollection = "org.freedesktop.Secret.Collection";
constexpr auto kIfaceItem = "org.freedesktop.Secret.Item";

// The Secret Service "Secret" struct: (oayays) = session, parameters, value, content_type.
struct SecretValue {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &arg, const SecretValue &s)
{
    arg.beginStructure();
    arg << s.session << s.parameters << s.value << s.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, SecretValue &s)
{
    arg.beginStructure();
    arg >> s.session >> s.parameters >> s.value >> s.contentType;
    arg.endStructure();
    return arg;
}

QMap<QString, QString> attributesFor(const QString &key)
{
    return { { QStringLiteral("application"), QStringLiteral("klorophylle") },
             { QStringLiteral("key"), key } };
}

QDBusMessage serviceCall(const QString &method)
{
    return QDBusMessage::createMethodCall(QString::fromLatin1(kService),
                                          QString::fromLatin1(kServicePath),
                                          QString::fromLatin1(kIfaceService), method);
}

bool ok(const QDBusMessage &reply)
{
    return reply.type() == QDBusMessage::ReplyMessage;
}

} // namespace

Q_DECLARE_METATYPE(SecretValue)

namespace klr {

FreedesktopSecretStore::FreedesktopSecretStore()
{
    qDBusRegisterMetaType<SecretValue>();
}

bool FreedesktopSecretStore::ensureSession() const
{
    if (!m_session.path().isEmpty() && m_session.path() != QStringLiteral("/"))
        return true;
    if (!QDBusConnection::sessionBus().isConnected())
        return false;

    QDBusMessage call = serviceCall(QStringLiteral("OpenSession"));
    call << QStringLiteral("plain") << QVariant::fromValue(QDBusVariant(QString()));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
    if (!ok(reply) || reply.arguments().size() < 2)
        return false;
    m_session = reply.arguments().at(1).value<QDBusObjectPath>();
    return !m_session.path().isEmpty() && m_session.path() != QStringLiteral("/");
}

void FreedesktopSecretStore::setSecret(const QString &key, const QString &value)
{
    if (!ensureSession())
        return;

    // Best-effort unlock of the default collection (no-op if already unlocked; interactive
    // prompts are not driven here — most keyrings auto-unlock at login).
    QDBusMessage unlock = serviceCall(QStringLiteral("Unlock"));
    unlock << QVariant::fromValue(QList<QDBusObjectPath>{ QDBusObjectPath(
        QString::fromLatin1(kDefaultCollection)) });
    QDBusConnection::sessionBus().call(unlock);

    SecretValue secretStruct{ m_session, {}, value.toUtf8(), QStringLiteral("text/plain") };

    QVariantMap properties;
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"),
                      QStringLiteral("Klorophylle: %1").arg(key));
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
                      QVariant::fromValue(attributesFor(key)));

    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kDefaultCollection),
        QString::fromLatin1(kIfaceCollection), QStringLiteral("CreateItem"));
    call << properties << QVariant::fromValue(secretStruct) << true; // replace existing
    QDBusConnection::sessionBus().call(call);
}

std::optional<QString> FreedesktopSecretStore::secret(const QString &key) const
{
    if (!ensureSession())
        return std::nullopt;

    QDBusMessage search = serviceCall(QStringLiteral("SearchItems"));
    search << QVariant::fromValue(attributesFor(key));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(search);
    if (!ok(reply) || reply.arguments().isEmpty())
        return std::nullopt;

    QList<QDBusObjectPath> items = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0));
    if (items.isEmpty() && reply.arguments().size() > 1) {
        // Only locked matches: try to unlock them, then read.
        const QList<QDBusObjectPath> locked =
            qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
        if (!locked.isEmpty()) {
            QDBusMessage unlock = serviceCall(QStringLiteral("Unlock"));
            unlock << QVariant::fromValue(locked);
            QDBusConnection::sessionBus().call(unlock);
            items = locked;
        }
    }
    if (items.isEmpty())
        return std::nullopt;

    QDBusMessage get = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), items.first().path(), QString::fromLatin1(kIfaceItem),
        QStringLiteral("GetSecret"));
    get << QVariant::fromValue(m_session);
    const QDBusMessage secretReply = QDBusConnection::sessionBus().call(get);
    if (!ok(secretReply) || secretReply.arguments().isEmpty())
        return std::nullopt;

    const SecretValue out = qdbus_cast<SecretValue>(secretReply.arguments().at(0));
    return QString::fromUtf8(out.value);
}

void FreedesktopSecretStore::removeSecret(const QString &key)
{
    if (!ensureSession())
        return;

    QDBusMessage search = serviceCall(QStringLiteral("SearchItems"));
    search << QVariant::fromValue(attributesFor(key));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(search);
    if (!ok(reply))
        return;

    QList<QDBusObjectPath> items;
    for (const QVariant &arg : reply.arguments())
        items += qdbus_cast<QList<QDBusObjectPath>>(arg);
    for (const QDBusObjectPath &item : items) {
        QDBusMessage del = QDBusMessage::createMethodCall(QString::fromLatin1(kService),
                                                          item.path(),
                                                          QString::fromLatin1(kIfaceItem),
                                                          QStringLiteral("Delete"));
        QDBusConnection::sessionBus().call(del);
    }
}

} // namespace klr
