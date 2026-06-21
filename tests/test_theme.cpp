// SPDX-License-Identifier: GPL-3.0-or-later
// Guards the design tokens (the design system) against drift, and that
// Format resolves labels through the unit-tested core formatters. Token *values* are
// the contract here — a colour/spacing change must update both the table and this test.
#include "themecontroller.h"
#include "tokens.h"
#include "uiformat.h"

#include "carestatus.h" // klr_core: CareStatus / CareLevel
#include "liveness.h"   // klr_core: Liveness / kConnectivityConnected
#include "reading.h"    // klr_core: Quantity, Unit, kQuantityCount

#include <QtTest/QtTest>

using namespace klr;

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    // LIGHT palette matches the the design system "Color tokens (LIGHT)" table.
    void lightPaletteMatchesDesignSystem()
    {
        ThemeController t; // defaults to Light
        QCOMPARE(t.colorScheme(), ThemeController::ColorScheme::Light);
        QVERIFY(!t.darkActive());

        QCOMPARE(t.colorBackground(), QColor(QStringLiteral("#f2fbfb")));
        QCOMPARE(t.colorCard(), QColor(QStringLiteral("#ffffff")));
        QCOMPARE(t.colorCardBorder(), QColor(QStringLiteral("#bfc9c2")));
        QCOMPARE(t.colorText(), QColor(QStringLiteral("#151d1d")));
        QCOMPARE(t.colorTextVariant(), QColor(QStringLiteral("#404944")));
        QCOMPARE(t.colorPrimary(), QColor(QStringLiteral("#003423")));
        QCOMPARE(t.colorGood(), QColor(QStringLiteral("#39a339")));   // Leaf Green
        QCOMPARE(t.colorBad(), QColor(QStringLiteral("#ba1a1a")));    // error
        QCOMPARE(t.colorAI(), QColor(QStringLiteral("#00cfff")));     // Cyan (AI/sensor only)
        QCOMPARE(t.colorWarn(), QColor(QStringLiteral("#f5a623")));   // derived amber
    }

    // Switching scheme flips darkActive, repaints the canvas, and notifies once.
    void darkSchemeSwitches()
    {
        ThemeController t;
        QSignalSpy spy(&t, &ThemeController::colorsChanged);

        t.setColorScheme(ThemeController::ColorScheme::Dark);
        QCOMPARE(spy.count(), 1);
        QVERIFY(t.darkActive());
        QCOMPARE(t.colorBackground(), QColor::fromRgb(tokens::kDark.background | 0xff000000u));
        QVERIFY(t.colorBackground() != QColor(QStringLiteral("#f2fbfb")));

        // Idempotent: setting the same scheme does not re-notify.
        t.setColorScheme(ThemeController::ColorScheme::Dark);
        QCOMPARE(spy.count(), 1);
    }

    // Type ramp, spacing rhythm and radii match the design system.
    void scaleTokens()
    {
        ThemeController t;
        QCOMPARE(t.fontDisplay(), QStringLiteral("Montserrat"));
        QCOMPARE(t.fontBody(), QStringLiteral("Inter"));
        QCOMPARE(t.fontSizeDisplay(), 48);
        QCOMPARE(t.fontSizeHeadline(), 32);
        QCOMPARE(t.fontSizeBody(), 16);
        QCOMPARE(t.fontSizeCaption(), 12);

        QCOMPARE(t.spacingXs(), 4);
        QCOMPARE(t.spacingBase(), 8);
        QCOMPARE(t.spacingSm(), 12);
        QCOMPARE(t.spacingMd(), 24);
        QCOMPARE(t.gutter(), 24);
        QCOMPARE(t.marginCompact(), 16);
        QCOMPARE(t.marginPage(), 64);

        QCOMPARE(t.radius(), 8);
        QCOMPARE(t.radiusFull(), 9999);
    }

    // Elevation tokens (the design system): the soft ambient shadow + modal backdrop
    // scrim are a fixed Dark-Emerald tint (rgb(0,77,54)) in BOTH schemes, with the spec
    // alphas (~0.08 shadow, ~0.32 scrim) and the `0 10px 30px` geometry.
    void elevationTokens()
    {
        ThemeController t;
        QCOMPARE(t.colorShadow(), QColor(0, 77, 54, 20));
        QCOMPARE(t.colorBackdropScrim(), QColor(0, 77, 54, 82));
        QCOMPARE(t.elevationOffsetY(), 10);
        QCOMPARE(t.elevationBlur(), 30);

        // Scheme-independent: the same dark tint after switching to Dark (a scrim must
        // darken the backdrop in both schemes — it must NOT follow the inverting primary).
        t.setColorScheme(ThemeController::ColorScheme::Dark);
        QCOMPARE(t.colorShadow(), QColor(0, 77, 54, 20));
        QCOMPARE(t.colorBackdropScrim(), QColor(0, 77, 54, 82));
    }

    // Format wraps the core formatters and bounds-checks the enum ints from QML.
    void formatWrapsCore()
    {
        Format f;
        QVERIFY(!f.quantityLabel(static_cast<int>(Quantity::SoilMoisture)).isEmpty());
        QVERIFY(!f.quantityLabel(kQuantityCount - 1).isEmpty());
        QVERIFY(f.quantityLabel(-1).isEmpty());
        QVERIFY(f.quantityLabel(kQuantityCount).isEmpty());

        QVERIFY(!f.unitSymbol(static_cast<int>(Unit::Percent)).isEmpty());
        QVERIFY(f.unitSymbol(-1).isEmpty());
    }

    // Care-status -> semantic colour (Theme) and -> label (Format), the care mapping.
    void careStatusAndLevelMapping()
    {
        ThemeController t; // Light
        QCOMPARE(t.careStatusColor(int(CareStatus::Ideal)), t.colorGood());
        QCOMPARE(t.careStatusColor(int(CareStatus::TooLow)), t.colorWarn());
        QCOMPARE(t.careStatusColor(int(CareStatus::TooHigh)), t.colorWarn());
        QCOMPARE(t.careStatusColor(int(CareStatus::Unknown)), t.colorTextVariant());
        QCOMPARE(t.careLevelColor(int(CareLevel::Good)), t.colorGood());
        QCOMPARE(t.careLevelColor(int(CareLevel::Attention)), t.colorWarn());
        QCOMPARE(t.careLevelColor(int(CareLevel::Unknown)), t.colorTextVariant());

        Format f;
        QCOMPARE(f.careStatusLabel(int(CareStatus::TooLow)), QStringLiteral("Too low"));
        QCOMPARE(f.careStatusLabel(int(CareStatus::Ideal)), QStringLiteral("Ideal"));
        QVERIFY(f.careStatusLabel(int(CareStatus::Unknown)).isEmpty());
        QCOMPARE(f.careLevelLabel(int(CareLevel::Attention)), QStringLiteral("Needs attention"));
        QVERIFY(f.careLevelLabel(int(CareLevel::Unknown)).isEmpty());

        // The compact status-badge glyph: a Material-Symbols ligature per level.
        QCOMPARE(f.careLevelIcon(int(CareLevel::Good)), QStringLiteral("check_circle"));
        QCOMPARE(f.careLevelIcon(int(CareLevel::Attention)), QStringLiteral("warning"));
        QVERIFY(f.careLevelIcon(int(CareLevel::Unknown)).isEmpty());
    }

    void livenessColorMapping()
    {
        ThemeController t; // Light
        QCOMPARE(t.livenessColor(int(Liveness::Live)), t.colorGood());
        QCOMPARE(t.livenessColor(int(Liveness::Stale)), t.colorWarn());
        QCOMPARE(t.livenessColor(int(Liveness::Offline)), t.colorBad());
        QCOMPARE(t.livenessColor(-1), t.colorTextVariant()); // no sensor / not judgeable
        // An open GATT connection renders blue (the sensor accent), distinct from offline red —
        // so the device we are talking to doesn't show as dead while it's off the air.
        QCOMPARE(t.livenessColor(kConnectivityConnected), t.colorAI());
        QVERIFY(t.livenessColor(kConnectivityConnected) != t.livenessColor(int(Liveness::Offline)));
    }

    // Chart line colours: every quantity gets a valid accent that is NOT the green ideal-
    // band colour (so the line stays legible over the band), with a few reading-type anchors.
    void quantityChartColours()
    {
        ThemeController t;
        for (int q = 0; q < kQuantityCount; ++q) {
            const QColor c = t.quantityColor(q);
            QVERIFY(c.isValid());
            QVERIFY(c != t.colorGood()); // never collide with the ideal-range band
        }
        // water = blue-ish, temperature = red-ish, light = amber.
        QVERIFY(t.quantityColor(int(Quantity::SoilMoisture)).blue()
                > t.quantityColor(int(Quantity::SoilMoisture)).red());
        QVERIFY(t.quantityColor(int(Quantity::AirTemperature)).red()
                > t.quantityColor(int(Quantity::AirTemperature)).blue());
        QVERIFY(t.quantityColor(int(Quantity::Illuminance)).red() > 200
                && t.quantityColor(int(Quantity::Illuminance)).green() > 150);
        // DLI is a light metric — it shares the amber light family.
        QCOMPARE(t.quantityColor(int(Quantity::Dli)), t.quantityColor(int(Quantity::Illuminance)));
        // Out-of-range stays valid (fallback), never crashes.
        QVERIFY(t.quantityColor(kQuantityCount).isValid());
        QVERIFY(t.quantityColor(-1).isValid());
    }

    // The responsive shell binds Theme.formFactor to formFactorForWidth(window.width):
    // < 600 -> Phone, < 1000 -> Tablet, >= 1000 -> Desktop. Guard the boundaries.
    void formFactorBreakpoints()
    {
        using FF = ThemeController::FormFactor;
        ThemeController t;
        QCOMPARE(t.formFactorForWidth(0), FF::Phone);
        QCOMPARE(t.formFactorForWidth(599), FF::Phone);
        QCOMPARE(t.formFactorForWidth(600), FF::Tablet);
        QCOMPARE(t.formFactorForWidth(999), FF::Tablet);
        QCOMPARE(t.formFactorForWidth(1000), FF::Desktop);
        QCOMPARE(t.formFactorForWidth(1920), FF::Desktop);
    }
};

// Guiless (QCoreApplication): no display/platform plugin needed in CI. ThemeController
// null-guards QGuiApplication::styleHints(), and these cases use explicit Light/Dark.
QTEST_GUILESS_MAIN(TestTheme)
#include "test_theme.moc"
