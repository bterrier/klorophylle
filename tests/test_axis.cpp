// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "axis.h"

using namespace klr;

namespace {
constexpr double kHour = 3'600'000.0;
constexpr double kDay = 86'400'000.0;
} // namespace

// The "nice axis" helper QtGraphs needs because it does not auto-range. Pure, literal
// inputs. See ../docs/adr/0006-history-charts-import.md.
class TestAxis : public QObject {
    Q_OBJECT

private slots:
    void roundsOutToNiceBounds()
    {
        // Soil moisture 23..78 % with ~5 ticks → round out to 20..80 step 20.
        const AxisRange a = niceAxis(23.0, 78.0, 5);
        QCOMPARE(a.min, 20.0);
        QCOMPARE(a.max, 80.0);
        QCOMPARE(a.tickInterval, 20.0);
        // Bounds always contain the data.
        QVERIFY(a.min <= 23.0);
        QVERIFY(a.max >= 78.0);
    }

    void smallRange()
    {
        // A tight 18.2..19.6 °C range → sub-degree tick, bounds still contain the data.
        const AxisRange a = niceAxis(18.2, 19.6, 5);
        QVERIFY(a.min <= 18.2);
        QVERIFY(a.max >= 19.6);
        QVERIFY(a.tickInterval > 0.0);
        QVERIFY(a.tickInterval <= 1.0);
    }

    void flatRangeIsPadded()
    {
        // A constant series must not collapse to a zero-height axis.
        const AxisRange a = niceAxis(50.0, 50.0, 5);
        QVERIFY(a.min < 50.0);
        QVERIFY(a.max > 50.0);
        QVERIFY(a.tickInterval > 0.0);
    }

    void invertedInputIsSwapped()
    {
        const AxisRange a = niceAxis(80.0, 20.0, 5);
        QCOMPARE(a.min, 20.0);
        QCOMPARE(a.max, 80.0);
    }

    void zeroValueFlat()
    {
        // Degenerate all-zero series: padded symmetrically, no NaN/inf.
        const AxisRange a = niceAxis(0.0, 0.0, 5);
        QVERIFY(a.min < 0.0);
        QVERIFY(a.max > 0.0);
        QVERIFY(qIsFinite(a.tickInterval) && a.tickInterval > 0.0);
    }

    // ---- niceTimeAxis: bounds SNAPPED to a round step + a DIVISION COUNT + label fmt ----
    // QtGraphs clamps DateTimeAxis.tickInterval to [0,100] and treats it as a division
    // count; niceTimeAxis snaps min/max to a round calendar step so ticks land on the
    // hour/day boundary (the X axis now matches niceAxis' snapping on Y). See ADR 0006.

    void timeIntradayTicksLandOnTheHour()
    {
        // 13:47 → 19:47 (a 6 h window) snaps OUT to 13:00 → 20:00 with hourly ticks.
        const double tMin = 5 * kDay + 13 * kHour + 47 * 60'000.0;
        const double tMax = tMin + 6 * kHour;
        const TimeAxis t = niceTimeAxis(tMin, tMax, 5);
        QCOMPARE(t.minMs, 5 * kDay + 13 * kHour); // floored to 13:00
        QCOMPARE(t.maxMs, 5 * kDay + 20 * kHour); // ceiled to 20:00
        QCOMPARE(t.tickInterval, 7.0);            // 7 one-hour divisions
        QCOMPARE(t.labelFormat, QStringLiteral("HH:mm"));
    }

    void timeBoundsAlwaysContainTheData()
    {
        // Across scales + non-aligned offsets: snapped bounds enclose the data, the count
        // is a sane integer within QtGraphs' clamp, and min/max are step-aligned (min<max).
        for (double span : { 4 * kHour, 6 * kHour, 12 * kHour, 10 * kDay, 240 * kDay }) {
            const double tMin = 5 * kDay + 13 * kHour + 47 * 60'000.0; // arbitrary offset
            const TimeAxis t = niceTimeAxis(tMin, tMin + span, 5);
            QVERIFY(t.minMs <= tMin);
            QVERIFY(t.maxMs >= tMin + span);
            QVERIFY(t.maxMs > t.minMs);
            QVERIFY(t.tickInterval >= 1.0 && t.tickInterval <= 100.0);
            QVERIFY(qIsFinite(t.tickInterval));
        }
    }

    void timeIntradaySpanGetsClockLabels()
    {
        QCOMPARE(niceTimeAxis(0.0, 6 * kHour, 5).labelFormat, QStringLiteral("HH:mm"));
    }

    void timeMultiDaySpanGetsDayLabels()
    {
        QCOMPARE(niceTimeAxis(0.0, 10 * kDay, 5).labelFormat, QStringLiteral("MMM d"));
    }

    void timeMultiMonthSpanGetsMonthYearLabels()
    {
        QCOMPARE(niceTimeAxis(0.0, 240 * kDay, 5).labelFormat, QStringLiteral("MMM yyyy"));
    }

    void timeHugeSpanCoarsensWithinTheClamp()
    {
        // A multi-year span would blow past 100 daily/monthly ticks: the step coarsens so
        // the division count stays within QtGraphs' [0,100] clamp, bounds still enclosing.
        const TimeAxis t = niceTimeAxis(0.0, 4000 * kDay, 5);
        QVERIFY(t.tickInterval >= 1.0 && t.tickInterval <= 100.0);
        QVERIFY(t.minMs <= 0.0 && t.maxMs >= 4000 * kDay);
    }

    void timeDegenerateSpanIsSafe()
    {
        // A single instant (zero span) still yields a valid window + count + format, no NaN.
        const TimeAxis t = niceTimeAxis(1'000'000.0, 1'000'000.0, 5);
        QVERIFY(qIsFinite(t.tickInterval) && t.tickInterval >= 1.0);
        QVERIFY(t.maxMs > t.minMs); // expanded to a real (one-hour) window
        QVERIFY(!t.labelFormat.isEmpty());
    }

    void timeTargetTicksControlsStep()
    {
        // Fewer requested ticks → a coarser round step (fewer divisions) over one span.
        QCOMPARE(niceTimeAxis(0.0, 12 * kHour, 5).tickInterval, 4.0); // 3h step → 4 divisions
        QCOMPARE(niceTimeAxis(0.0, 12 * kHour, 3).tickInterval, 2.0); // 6h step → 2 divisions
        QVERIFY(niceTimeAxis(0.0, 12 * kHour, 1).tickInterval >= 1.0); // clamped sane minimum
    }
};

QTEST_GUILESS_MAIN(TestAxis)
#include "test_axis.moc"
