// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "aggregate.h"
#include "reading.h"

using namespace klr;

// Pure multi-sensor aggregation: NewestWins is implemented; Average is deferred and
// falls back to newest. See docs/adr/0005-plant-sensor-binding.md.
class TestAggregate : public QObject {
    Q_OBJECT

    static Reading soil(double v, const QDateTime &ts)
    {
        return { Quantity::SoilMoisture, v, Unit::Percent, ts, Provenance::Advertisement };
    }

private slots:
    void emptyYieldsNullopt() { QVERIFY(!aggregate({}, AggregationPolicy::NewestWins).has_value()); }

    void allAbsentYieldsNullopt()
    {
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const Reading absent[] = {
            { Quantity::SoilMoisture, std::nullopt, Unit::Percent, t0, Provenance::Advertisement },
            { Quantity::SoilMoisture, std::nullopt, Unit::Percent, t0.addSecs(60),
              Provenance::Advertisement },
        };
        QVERIFY(!aggregate(absent, AggregationPolicy::NewestWins).has_value());
    }

    void newestWinsPicksLatestTimestamp()
    {
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const Reading rs[] = {
            soil(40.0, t0),
            soil(55.0, t0.addSecs(3600)), // newest
            soil(48.0, t0.addSecs(1800)),
        };
        const auto r = aggregate(rs, AggregationPolicy::NewestWins);
        QVERIFY(r.has_value());
        QCOMPARE(r->value.value(), 55.0);
        QCOMPARE(r->timestamp, t0.addSecs(3600));
    }

    void newestWinsSkipsAbsentEvenIfLater()
    {
        // A later-but-absent sample must not shadow the freshest present value.
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const Reading rs[] = {
            soil(42.0, t0),
            { Quantity::SoilMoisture, std::nullopt, Unit::Percent, t0.addSecs(3600),
              Provenance::Advertisement },
        };
        const auto r = aggregate(rs, AggregationPolicy::NewestWins);
        QVERIFY(r.has_value());
        QCOMPARE(r->value.value(), 42.0);
    }

    void averageFallsBackToNewestForNow()
    {
        // Deferred: Average is not implemented yet and must resolve to newest-wins.
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const Reading rs[] = { soil(40.0, t0), soil(60.0, t0.addSecs(60)) };
        const auto r = aggregate(rs, AggregationPolicy::Average);
        QVERIFY(r.has_value());
        QCOMPARE(r->value.value(), 60.0); // newest, NOT the 50.0 mean
    }
};

QTEST_APPLESS_MAIN(TestAggregate)
#include "test_aggregate.moc"
