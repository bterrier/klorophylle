// SPDX-License-Identifier: GPL-3.0-or-later
#include "navigationcontroller.h"

#include "log.h"

#include <QtQml/QQmlEngine>

namespace klr {

NavigationController *NavigationController::s_instance = nullptr;

NavigationController::NavigationController(QObject *parent) : QObject(parent) {}

NavigationController *NavigationController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "NavigationController::create",
               "composition root must set NavigationController::s_instance before loading QML");
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

void NavigationController::push(Route route, const QVariantMap &args)
{
    // Idempotent on the top of the stack: re-pushing the page already shown (e.g. tapping
    // "Settings" in the overflow menu while Settings is open) must NOT stack a second copy.
    // A duplicate would create a second live screen instance — and, for a screen that wires a
    // Connections to a singleton (SettingsScreen's importFinished handler), a second result
    // dialog on every import. Distinct args (e.g. History for another quantity) still push.
    if (m_stack.last().first == route && m_stack.last().second == args)
        return;
    m_stack.append({ route, args });
    Q_EMIT pushed(route, args);
    Q_EMIT routeChanged();
}

void NavigationController::replace(Route route, const QVariantMap &args)
{
    // Collapse the detail back to the section root, then show this route as the sole
    // detail page — selecting a different master item swaps the detail, never stacks it.
    m_stack = { { m_section, {} }, { route, args } };
    Q_EMIT replaced(route, args);
    Q_EMIT routeChanged();
}

void NavigationController::pop()
{
    if (m_stack.size() <= 1)
        return; // at a section home — nothing to pop
    m_stack.removeLast();
    Q_EMIT popped();
    Q_EMIT routeChanged();
}

void NavigationController::goSection(Route section)
{
    if (!isSection(section)) {
        qCWarning(lcApp) << "goSection ignored: not a top-level section" << int(section);
        return;
    }
    m_section = section;
    m_stack = { { section, {} } }; // reset to this section's home
    Q_EMIT sectionReset(section);
    Q_EMIT routeChanged();
}

void NavigationController::goPage(Route route)
{
    // Already showing this page (e.g. tapping AI on the rail while the AI page is open): a
    // no-op, so the live screen — and its state, like the chat scroll position — survives.
    if (currentRoute() == route)
        return;
    // Sit the page as the SOLE detail over the current section root, never stacked on an open
    // detail; back returns to the section home, like the other full-width pages.
    m_stack = { { m_section, {} }, { route, {} } };
    Q_EMIT replaced(route, {});
    Q_EMIT routeChanged();
}

} // namespace klr
