// SPDX-License-Identifier: GPL-3.0-or-later
#include "iconbase.h"

namespace klr {

IconBase::IconBase(QQuickItem *parent)
    : QQuickItem(parent)
    , m_icon(new IconInfo(this))
{
}

} // namespace klr
