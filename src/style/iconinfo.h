// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtGui/QColor>
#include <QtQml/qqmlregistration.h>

namespace klr {

// The value behind the Icon component's `icon` grouped property — our own stand-in for
// Qt's private QQuickIcon, so we can type the property correctly in C++ and drive it
// from QML as `icon.name: "…"` / `icon.source: "…"` / `icon.color` / `icon.size`.
// A QObject (not a gadget) so the sub-properties are individually bindable. ANONYMOUS:
// it is only ever reached through IconBase.icon, never instantiated directly in QML.
class IconInfo : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // The Material Symbols ligature (e.g. "local_florist"); renders as a glyph.
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    // An image URL (e.g. an SVG); takes precedence is left to the component.
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY changed)
    // Tint. Invalid (default) means "use the surrounding text colour".
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    // Pixel size; <= 0 (default) means "use the component default".
    Q_PROPERTY(qreal size READ size WRITE setSize NOTIFY changed)

public:
    explicit IconInfo(QObject *parent = nullptr) : QObject(parent) {}

    QString name() const { return m_name; }
    void setName(const QString &v) { if (v != m_name) { m_name = v; emit changed(); } }
    QUrl source() const { return m_source; }
    void setSource(const QUrl &v) { if (v != m_source) { m_source = v; emit changed(); } }
    QColor color() const { return m_color; }
    void setColor(const QColor &v) { if (v != m_color) { m_color = v; emit changed(); } }
    qreal size() const { return m_size; }
    void setSize(qreal v) { if (v != m_size) { m_size = v; emit changed(); } }

signals:
    void changed();

private:
    QString m_name;
    QUrl m_source;
    QColor m_color; // invalid by default
    qreal m_size = -1;
};

} // namespace klr
