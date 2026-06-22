// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "navigationcontroller.h"

using namespace klr;
using Route = NavigationController::Route;

// NavigationController: the typed route stack the QML shell mirrors. Pure C++ — no QML —
// so the navigation rules (back-availability, section reset, More-screen-over-section)
// are unit-tested without instantiating the UI.
class TestNavigation : public QObject {
    Q_OBJECT

private slots:
    void initialStateIsPlantsHome()
    {
        NavigationController nav;
        QCOMPARE(nav.currentRoute(), Route::Plants);
        QVERIFY(!nav.canGoBack());
        QCOMPARE(nav.currentSection(), Route::Plants);
    }

    void pushAdvancesRouteAndEmits()
    {
        NavigationController nav;
        QSignalSpy pushed(&nav, &NavigationController::pushed);
        QSignalSpy routeChanged(&nav, &NavigationController::routeChanged);

        nav.push(Route::PlantDetail);
        QCOMPARE(nav.currentRoute(), Route::PlantDetail);
        QVERIFY(nav.canGoBack());
        QCOMPARE(pushed.size(), 1);
        QCOMPARE(qvariant_cast<Route>(pushed.first().at(0)), Route::PlantDetail);
        QCOMPARE(routeChanged.size(), 1);
    }

    void pushCarriesArgs()
    {
        NavigationController nav;
        QSignalSpy pushed(&nav, &NavigationController::pushed);

        nav.push(Route::History, QVariantMap{ { QStringLiteral("quantity"), 3 } });
        QCOMPARE(pushed.size(), 1);
        const QVariantMap args = pushed.first().at(1).toMap();
        QCOMPARE(args.value(QStringLiteral("quantity")).toInt(), 3);
    }

    void pushIsIdempotentOnTopOfStack()
    {
        NavigationController nav;
        nav.push(Route::Settings);
        QSignalSpy pushed(&nav, &NavigationController::pushed);

        // Re-pushing the page already on top (e.g. tapping Settings in the menu again) is a
        // no-op — no second instance, no duplicate screen (and thus no duplicate import dialog).
        nav.push(Route::Settings);
        QCOMPARE(pushed.size(), 0);
        QCOMPARE(nav.currentRoute(), Route::Settings);
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::Plants); // only one Settings was ever on the stack

        // Distinct args are a genuinely different page, so they still push.
        nav.push(Route::History, QVariantMap{ { QStringLiteral("quantity"), 1 } });
        QSignalSpy pushed2(&nav, &NavigationController::pushed);
        nav.push(Route::History, QVariantMap{ { QStringLiteral("quantity"), 2 } });
        QCOMPARE(pushed2.size(), 1);
    }

    void settingsCategoryIsParameterizedAndFullPage()
    {
        NavigationController nav;
        QSignalSpy pushed(&nav, &NavigationController::pushed);

        // The Settings index deep-links to a single category page via one parameterized route.
        nav.push(Route::SettingsCategory, QVariantMap{ { QStringLiteral("which"), QStringLiteral("ai") } });
        QCOMPARE(nav.currentRoute(), Route::SettingsCategory);
        QVERIFY(nav.currentIsFullPage()); // full-width like Settings — master pane hides
        QCOMPARE(pushed.size(), 1);
        QCOMPARE(pushed.first().at(1).toMap().value(QStringLiteral("which")).toString(),
                 QStringLiteral("ai"));

        // A different category is a genuinely different page (distinct args) — it still pushes,
        // so navigating index -> Units -> back -> AI works.
        nav.push(Route::SettingsCategory, QVariantMap{ { QStringLiteral("which"), QStringLiteral("units") } });
        QCOMPARE(pushed.size(), 2);
        QCOMPARE(nav.currentRoute(), Route::SettingsCategory);

        // The same category re-pushed (e.g. tapping the same row again) is idempotent.
        nav.push(Route::SettingsCategory, QVariantMap{ { QStringLiteral("which"), QStringLiteral("units") } });
        QCOMPARE(pushed.size(), 2);
    }

    void popReversesAndStopsAtRoot()
    {
        NavigationController nav;
        nav.push(Route::PlantDetail);
        nav.push(Route::PlantSettings);
        QSignalSpy popped(&nav, &NavigationController::popped);

        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::PlantDetail);
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::Plants);
        QVERIFY(!nav.canGoBack());
        QCOMPARE(popped.size(), 2);

        nav.pop(); // at the root — no-op, no further signal
        QCOMPARE(nav.currentRoute(), Route::Plants);
        QCOMPARE(popped.size(), 2);
    }

    void replaceCollapsesDetailToOnePage()
    {
        NavigationController nav;
        nav.push(Route::PlantDetail);
        nav.push(Route::History); // dug two deep into the Plants detail
        QSignalSpy replaced(&nav, &NavigationController::replaced);

        // Selecting another master item shows its detail as the SOLE detail page.
        nav.replace(Route::PlantDetail);
        QCOMPARE(nav.currentRoute(), Route::PlantDetail);
        QVERIFY(nav.canGoBack());      // still above the section root...
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::Plants); // ...but only one level deep
        QVERIFY(!nav.canGoBack());
        QCOMPARE(replaced.size(), 1);
        QCOMPARE(qvariant_cast<Route>(replaced.first().at(0)), Route::PlantDetail);
    }

    void replaceKeepsCurrentSectionRoot()
    {
        NavigationController nav;
        nav.goSection(Route::Sensors);
        nav.replace(Route::Live);

        QCOMPARE(nav.currentRoute(), Route::Live);
        QCOMPARE(nav.currentSection(), Route::Sensors);
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::Sensors); // collapses to the Sensors root
    }

    void fullPageRoutesAreFlagged()
    {
        NavigationController nav;
        QVERIFY(!nav.currentIsFullPage()); // Plants home
        nav.push(Route::PlantDetail);
        QVERIFY(!nav.currentIsFullPage()); // section detail
        nav.push(Route::Settings);
        QVERIFY(nav.currentIsFullPage());  // app-level page
        nav.pop();
        QVERIFY(!nav.currentIsFullPage());
    }

    void goSectionResetsStackAndSwitchesSection()
    {
        NavigationController nav;
        nav.push(Route::PlantDetail); // dig into the Plants section
        QSignalSpy reset(&nav, &NavigationController::sectionReset);

        nav.goSection(Route::Sensors);
        QCOMPARE(nav.currentRoute(), Route::Sensors);
        QCOMPARE(nav.currentSection(), Route::Sensors);
        QVERIFY(!nav.canGoBack()); // reset to the section home
        QCOMPARE(reset.size(), 1);
        QCOMPARE(qvariant_cast<Route>(reset.first().at(0)), Route::Sensors);
    }

    void moreScreenPushKeepsCurrentSection()
    {
        NavigationController nav;
        nav.goSection(Route::Sensors);
        nav.push(Route::Settings); // a "More" screen pushed over the Sensors section

        QCOMPARE(nav.currentRoute(), Route::Settings);
        QVERIFY(nav.canGoBack());
        QCOMPARE(nav.currentSection(), Route::Sensors); // highlight stays on Sensors
    }

    void sensorDetailPushesOverSensorsAsDetail()
    {
        NavigationController nav;
        nav.goSection(Route::Sensors);
        nav.push(Route::SensorDetail); // tapping a registered sensor

        QCOMPARE(nav.currentRoute(), Route::SensorDetail);
        QVERIFY(nav.canGoBack());
        QVERIFY(!nav.currentIsFullPage());              // a section detail, not an app page
        QCOMPARE(nav.currentSection(), Route::Sensors); // highlight stays on Sensors
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::Sensors);   // back to the Sensors root
    }

    void goPageCollapsesDetailNeverStacks()
    {
        NavigationController nav;
        nav.push(Route::PlantDetail);
        nav.push(Route::History); // viewing a chart two deep in the Plants detail
        QSignalSpy replaced(&nav, &NavigationController::replaced);

        // Tapping the AI rail destination opens it as a top-level full page — it does NOT
        // stack on top of the open detail (the reported bug), it collapses to the section root.
        nav.goPage(Route::AIInsights);
        QCOMPARE(nav.currentRoute(), Route::AIInsights);
        QVERIFY(nav.currentIsFullPage());
        QCOMPARE(replaced.size(), 1);
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::Plants); // back goes to the section home, not History
        QVERIFY(!nav.canGoBack());
    }

    void goPageIsIdempotent()
    {
        NavigationController nav;
        nav.goPage(Route::AIInsights);
        QSignalSpy replaced(&nav, &NavigationController::replaced);

        // Re-tapping AI while the AI page already shows is a no-op — the live screen (and its
        // chat scroll state) is not torn down and rebuilt.
        nav.goPage(Route::AIInsights);
        QCOMPARE(replaced.size(), 0);
        QCOMPARE(nav.currentRoute(), Route::AIInsights);
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::Plants); // only ever one level deep
    }

    void goPageKeepsCurrentSection()
    {
        NavigationController nav;
        nav.goSection(Route::Sensors);
        nav.goPage(Route::AIInsights);

        QCOMPARE(nav.currentRoute(), Route::AIInsights);
        QCOMPARE(nav.currentSection(), Route::Sensors); // section unchanged underneath
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::Sensors); // back to the Sensors root
    }

    void goPageThenSubPagePushStacks()
    {
        NavigationController nav;
        nav.goPage(Route::AIInsights);
        // The AI page's own sub-navigation (the global journal) still STACKS on top of it,
        // so back returns to the AI page — goPage only governs the top-level entry.
        nav.push(Route::GlobalJournal);
        QCOMPARE(nav.currentRoute(), Route::GlobalJournal);
        nav.pop();
        QCOMPARE(nav.currentRoute(), Route::AIInsights);
    }

    void goSectionIgnoresNonSectionRoutes()
    {
        NavigationController nav;
        nav.goSection(Route::Sensors);
        QSignalSpy reset(&nav, &NavigationController::sectionReset);

        nav.goSection(Route::Settings); // not a top-level section — ignored
        QCOMPARE(nav.currentSection(), Route::Sensors);
        QCOMPARE(nav.currentRoute(), Route::Sensors);
        QCOMPARE(reset.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestNavigation)
#include "test_navigation.moc"
