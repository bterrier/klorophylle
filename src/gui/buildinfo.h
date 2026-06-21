// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

namespace klr {

// Read-only build/version metadata for the About screen, exposed to QML as `BuildInfo`.
// Self-contained (the engine default-constructs it) — it only reads compile-time
// constants (the generated klr/version.h, the single source of version truth) and
// qVersion() at runtime. No services, so no create() factory.
class BuildInfo : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BuildInfo)
    QML_SINGLETON

    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString buildType READ buildType CONSTANT)
    Q_PROPERTY(QString license READ license CONSTANT)
    Q_PROPERTY(QString sourceUrl READ sourceUrl CONSTANT)

public:
    explicit BuildInfo(QObject *parent = nullptr) : QObject(parent) {}

    QString appName() const;
    QString appVersion() const;
    QString qtVersion() const;
    QString buildType() const;
    QString license() const;
    QString sourceUrl() const;
};

} // namespace klr
