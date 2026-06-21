// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "carestatus.h" // CareStatus
#include "reading.h"    // Quantity
#include "uiformat.h"

using namespace klr;

// The presentation seam exposed to QML as `Format` (klr_style). Labels/glyphs are thin maps
// over the core formatters; the interesting case here is the notification text — and in
// particular the reframing that a dry-soil alert reads as "time to water", not a raw breach.
class TestFormat : public QObject {
    Q_OBJECT

    Format fmt;

private slots:
    void quantityAndStatusLabels()
    {
        QVERIFY(!fmt.quantityLabel(int(Quantity::SoilMoisture)).isEmpty());
        QCOMPARE(fmt.quantityLabel(-1), QString());            // out of range
        QCOMPARE(fmt.careStatusLabel(int(CareStatus::Unknown)), QString());
        QVERIFY(!fmt.careStatusLabel(int(CareStatus::TooLow)).isEmpty());
    }

    void notificationTitleNamesThePlant()
    {
        const QString t = fmt.notificationTitle(QStringLiteral("Basil"));
        QVERIFY(t.contains(QStringLiteral("Basil")));
        QVERIFY(!t.isEmpty());
    }

    void drySoilReadsAsTimeToWater()
    {
        // The defining notification reframing (ADR 0016): soil moisture TooLow is the "water me" signal,
        // phrased as the action — never the generic "soil moisture is too low".
        const QString body = fmt.notificationBody(int(Quantity::SoilMoisture), int(CareStatus::TooLow));
        QVERIFY(body.contains(QStringLiteral("water"), Qt::CaseInsensitive));
        // And it is NOT the generic "<quantity> is too low" phrasing.
        QVERIFY(!body.contains(fmt.quantityLabel(int(Quantity::SoilMoisture))));
    }

    void otherQuantitiesUseGenericPhrasing()
    {
        const QString low =
            fmt.notificationBody(int(Quantity::AirTemperature), int(CareStatus::TooLow));
        QVERIFY(low.contains(fmt.quantityLabel(int(Quantity::AirTemperature))));
        QVERIFY(low.contains(QStringLiteral("low")));

        const QString high =
            fmt.notificationBody(int(Quantity::AirHumidity), int(CareStatus::TooHigh));
        QVERIFY(high.contains(fmt.quantityLabel(int(Quantity::AirHumidity))));
        QVERIFY(high.contains(QStringLiteral("high")));

        // High soil moisture is NOT the watering case — it uses the generic "too high".
        const QString wet =
            fmt.notificationBody(int(Quantity::SoilMoisture), int(CareStatus::TooHigh));
        QVERIFY(!wet.contains(QStringLiteral("water"), Qt::CaseInsensitive));
        QVERIFY(wet.contains(QStringLiteral("high")));
    }

    void nonAlertingStatusHasNoBody()
    {
        // Only TooLow/TooHigh ever reach a notification; the neutral states produce nothing.
        QCOMPARE(fmt.notificationBody(int(Quantity::SoilMoisture), int(CareStatus::Ideal)), QString());
        QCOMPARE(fmt.notificationBody(int(Quantity::SoilMoisture), int(CareStatus::Unknown)), QString());
    }
};

QTEST_GUILESS_MAIN(TestFormat)
#include "test_format.moc"
