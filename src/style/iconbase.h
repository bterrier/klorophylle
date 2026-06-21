// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iconinfo.h"

#include <QtQuick/QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace klr {

// C++ base for the Icon QML component. It exists solely to declare the `icon` grouped
// property with a correct, public type (Qt's QQuickIcon is private), so QML can write
// `Icon { icon.name: "…" }`. The visual rendering (glyph Text vs Image) lives in
// Icon.qml, which derives from this. The IconInfo is owned by the item.
class IconBase : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(klr::IconInfo *icon READ icon CONSTANT)

public:
    explicit IconBase(QQuickItem *parent = nullptr);

    IconInfo *icon() const { return m_icon; }

private:
    IconInfo *m_icon;
};

} // namespace klr
