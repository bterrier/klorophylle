// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "format.h"
#include "reading.h"
#include "units.h"

using namespace klr;

// Pure unit-conversion + unit-aware-formatting tests: no settings, no QML, no DB.
// Storage stays canonical — these prove the *display* boundary converts correctly.
class TestUnits : public QObject {
    Q_OBJECT

private slots:
    void identityWhenSameUnit()
    {
        QCOMPARE(convert(20.0, Unit::DegreeCelsius, Unit::DegreeCelsius), 20.0);
        QCOMPARE(convert(1013.25, Unit::Hectopascal, Unit::Hectopascal), 1013.25);
    }

    void temperatureIsExact()
    {
        QCOMPARE(convert(0.0, Unit::DegreeCelsius, Unit::DegreeFahrenheit), 32.0);
        QCOMPARE(convert(100.0, Unit::DegreeCelsius, Unit::DegreeFahrenheit), 212.0);
        QCOMPARE(convert(20.0, Unit::DegreeCelsius, Unit::DegreeFahrenheit), 68.0);
    }

    void temperatureRoundTrips()
    {
        const double f = convert(21.5, Unit::DegreeCelsius, Unit::DegreeFahrenheit);
        QVERIFY(qFuzzyCompare(convert(f, Unit::DegreeFahrenheit, Unit::DegreeCelsius), 21.5));
    }

    void illuminanceApproxAndRoundTrips()
    {
        // Documented daylight approximation (lux × 0.0185).
        QVERIFY(qFuzzyCompare(convert(1000.0, Unit::Lux, Unit::Micromole), 18.5));
        QVERIFY(qFuzzyCompare(convert(18.5, Unit::Micromole, Unit::Lux), 1000.0));
    }

    void pressureConversions()
    {
        QVERIFY(qFuzzyCompare(convert(1000.0, Unit::Hectopascal, Unit::InchOfMercury), 29.5299830714));
        QVERIFY(qFuzzyCompare(convert(1000.0, Unit::Hectopascal, Unit::MillimetreOfMercury), 750.061682704));
        QVERIFY(qFuzzyCompare(convert(29.5299830714, Unit::InchOfMercury, Unit::Hectopascal), 1000.0));
        QVERIFY(qFuzzyCompare(convert(750.061682704, Unit::MillimetreOfMercury, Unit::Hectopascal), 1000.0));
    }

    void undefinedPairIsIdentity()
    {
        // No conversion between unrelated units — value unchanged, never garbage.
        QCOMPARE(convert(42.0, Unit::Percent, Unit::Lux), 42.0);
    }

    void displayUnitFollowsPreference()
    {
        DisplayUnits u; // defaults == canonical
        QCOMPARE(displayUnit(Quantity::AirTemperature, u), Unit::DegreeCelsius);
        QCOMPARE(displayUnit(Quantity::Illuminance, u), Unit::Lux);
        QCOMPARE(displayUnit(Quantity::Pressure, u), Unit::Hectopascal);

        u.temperature = TemperatureUnit::Fahrenheit;
        u.illuminance = IlluminanceUnit::Micromole;
        u.pressure = PressureUnit::MmHg;
        QCOMPARE(displayUnit(Quantity::AirTemperature, u), Unit::DegreeFahrenheit);
        QCOMPARE(displayUnit(Quantity::SoilTemperature, u), Unit::DegreeFahrenheit);
        QCOMPARE(displayUnit(Quantity::Illuminance, u), Unit::Micromole);
        QCOMPARE(displayUnit(Quantity::Pressure, u), Unit::MillimetreOfMercury);

        u.pressure = PressureUnit::InchHg;
        QCOMPARE(displayUnit(Quantity::Pressure, u), Unit::InchOfMercury);

        // A quantity with no unit preference keeps its canonical unit.
        QCOMPARE(displayUnit(Quantity::SoilMoisture, u), Unit::Percent);

        // DLI is a derived quantity: no canonical/display unit, no conversion.
        QCOMPARE(canonicalUnit(Quantity::Dli), Unit::None);
        QCOMPARE(displayUnit(Quantity::Dli, u), Unit::None);
        const DisplayUnits imperial { TemperatureUnit::Fahrenheit, IlluminanceUnit::Micromole };
        QCOMPARE(displayUnit(Quantity::Dli, imperial), Unit::None); // unaffected by preferences
    }

    void formatValueDefaultEqualsCanonical()
    {
        const Reading r { Quantity::AirTemperature, 20.0, Unit::DegreeCelsius, {}, Provenance::Live };
        QCOMPARE(formatValue(r, DisplayUnits {}), formatValue(r));
        QCOMPARE(formatValue(r, DisplayUnits {}), QStringLiteral("20.0 °C"));
    }

    void formatValueConvertsToPreference()
    {
        DisplayUnits u;
        u.temperature = TemperatureUnit::Fahrenheit;
        const Reading r { Quantity::AirTemperature, 20.0, Unit::DegreeCelsius, {}, Provenance::Live };
        QCOMPARE(formatValue(r, u), QStringLiteral("68.0 °F"));
    }

    void formatValueAbsentStaysEmDash()
    {
        DisplayUnits u;
        u.temperature = TemperatureUnit::Fahrenheit;
        const Reading r { Quantity::AirTemperature, std::nullopt, Unit::DegreeCelsius, {}, Provenance::Live };
        QCOMPARE(formatValue(r, u), QStringLiteral("—"));
    }

    void formatValuePressureDecimals()
    {
        DisplayUnits u;
        u.pressure = PressureUnit::InchHg;
        const Reading r { Quantity::Pressure, 1013.0, Unit::Hectopascal, {}, Provenance::Live };
        // inHg renders with two decimals.
        QCOMPARE(formatValue(r, u), QStringLiteral("29.91 inHg"));
    }
};

QTEST_GUILESS_MAIN(TestUnits)
#include "test_units.moc"
