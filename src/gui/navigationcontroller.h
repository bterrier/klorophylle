// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QVariantMap>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace klr {

// The single source of truth for shell navigation — a typed route stack in C++ so the QML
// shell carries no navigation logic (no section strings, no stack-depth math, no
// route->component+side-effect map). The StackView in Main.qml is a pure MIRROR driven by
// the pushed/popped/sectionReset signals; the route->QML-Component map stays in QML because
// Components are QML objects. An injected QML singleton via the set-once s_instance/create()
// pattern (ADR 0002 — no setContextProperty, not default-constructible), like AppContext.
class NavigationController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(Route currentRoute READ currentRoute NOTIFY routeChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY routeChanged)
    Q_PROPERTY(Route currentSection READ currentSection NOTIFY routeChanged)
    // Whether the top route is an app-level page (Settings/Export/About) rather than a
    // section detail. The shell hides the master pane for these — showing the plant/sensor
    // list beside Settings makes no sense — so they read as a full-width page.
    Q_PROPERTY(bool currentIsFullPage READ currentIsFullPage NOTIFY routeChanged)

public:
    // Only the routes that exist today; Dashboard/ProbePairing get added when those screens
    // land — the enum is the extension point.
    enum class Route {
        Plants,
        PlantDetail,
        PlantSettings,
        History,
        Sensors,
        Live,
        SensorDetail,
        Settings,
        // A single category page (Appearance/Units/Notifications/Sensors/AI/Data) of the
        // Settings index, carrying a string `which` arg in the push args map (e.g. {which:"ai"}).
        // One parameterized route, not one route per category — the page switches on `which`.
        // Full-width like Settings; lets the AI screen deep-link to its own settings page.
        SettingsCategory,
        Export,
        About,
        AIInsights, // the AI assistant chat — a full-width page, like Settings
        GlobalJournal, // the global (plant-less) journal — user-wide memory + notes, full-width
    };
    Q_ENUM(Route)

    explicit NavigationController(QObject *parent = nullptr);

    // Push a route on top of the stack (optionally carrying QML component args, e.g. the
    // history chart's quantity/label). Emits pushed() for the shell to mirror.
    Q_INVOKABLE void push(Route route, const QVariantMap &args = {});
    // Replace the whole detail with a single route: collapse everything above the section
    // root, then show this route. This is the "select a master item" verb — clicking plant
    // B while viewing plant A swaps the detail rather than stacking A->B. Emits replaced().
    Q_INVOKABLE void replace(Route route, const QVariantMap &args = {});
    // Drop the top route. No-op at the root (a section home). Emits popped().
    Q_INVOKABLE void pop();
    // Switch top-level section (Plants|Sensors): reset the stack to that section's home.
    // Emits sectionReset(). Other routes are ignored (sections are Plants/Sensors only).
    Q_INVOKABLE void goSection(Route section);
    // Open a full-width app page (e.g. the AI assistant) as a TOP-LEVEL destination from the
    // nav rail: collapse the detail to this single page over the current section root, so it
    // NEVER stacks on top of an open detail (a plant/sensor detail you happened to be viewing).
    // Idempotent when that page already shows — it is not torn down and rebuilt. Emits
    // replaced(), which the shell mirrors exactly like replace(). Contrast push() (used by the
    // "More" overflow), which stacks an excursion you return from to where you were.
    Q_INVOKABLE void goPage(Route route);

    Route currentRoute() const { return m_stack.last().first; }
    bool canGoBack() const { return m_stack.size() > 1; }
    Route currentSection() const { return m_section; }
    bool currentIsFullPage() const { return isFullPage(m_stack.last().first); }

    // QML obtains the composition-root instance through this factory (set-once global),
    // exactly as AppContext does. With no default-construction path in QML, the engine
    // cannot make its own un-wired instance.
    static NavigationController *s_instance;
    static NavigationController *create(QQmlEngine *, QJSEngine *);

signals:
    void routeChanged();
    // Intents the shell mirrors onto its StackView.
    void pushed(Route route, QVariantMap args);
    void popped();
    void sectionReset(Route section);
    void replaced(Route route, QVariantMap args);

private:
    static bool isSection(Route r) { return r == Route::Plants || r == Route::Sensors; }
    static bool isFullPage(Route r)
    {
        return r == Route::Settings || r == Route::SettingsCategory || r == Route::Export
            || r == Route::About || r == Route::AIInsights || r == Route::GlobalJournal;
    }

    QList<QPair<Route, QVariantMap>> m_stack{ { Route::Plants, {} } };
    Route m_section = Route::Plants; // last Plants/Sensors section; survives More-screen pushes
};

} // namespace klr
