// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "reading.h"
#include "seriesmodel.h"

using namespace klr;

// The chart view-model: points + a pre-computed "nice" axis from literal readings. No
// C++ touches a QML series — QML replaces SeriesModel::points into its own LineSeries
// (fixes the C++-touches-QML-series pitfall). See ADR 0006.
class TestSeriesModel : public QObject {
    Q_OBJECT

    static Reading soil(double v, const QDateTime &ts)
    {
        return { Quantity::SoilMoisture, v, Unit::Percent, ts, Provenance::History };
    }

private slots:
    void buildsPointsAndNiceAxis()
    {
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        SeriesModel m;
        QSignalSpy spy(&m, &SeriesModel::changed);

        m.setReadings({ soil(23.0, t0), soil(78.0, t0.addDays(1)), soil(50.0, t0.addDays(2)) });

        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.points().size(), 3);
        QVERIFY(!m.empty());

        // Y axis rounded out around 23..78 (see test_axis): contains the data.
        QVERIFY(m.axisMin() <= 23.0);
        QVERIFY(m.axisMax() >= 78.0);
        QVERIFY(m.tickInterval() > 0.0);

        // X axis spans the sample times (ms since epoch).
        QCOMPARE(m.tMin(), double(t0.toMSecsSinceEpoch()));
        QCOMPARE(m.tMax(), double(t0.addDays(2).toMSecsSinceEpoch()));

        // X ticks (niceTimeAxis): a DateTimeAxis tickInterval is a DIVISION COUNT, so the
        // model exposes a small value within QtGraphs' [0,100] clamp (never a ms span that
        // would flood the axis) plus a non-empty label format. Thresholds: see test_axis.
        QVERIFY(m.xTickInterval() >= 1.0 && m.xTickInterval() <= 100.0);
        QVERIFY(!m.xLabelFormat().isEmpty());

        // Points are (ms, value) in order, via the x/y roles too.
        QCOMPARE(m.data(m.index(0, 0), SeriesModel::YRole).toDouble(), 23.0);
        QCOMPARE(m.data(m.index(0, 0), SeriesModel::XRole).toDouble(),
                 double(t0.toMSecsSinceEpoch()));
    }

    void skipsAbsentValues()
    {
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        SeriesModel m;
        const Reading gap{ Quantity::SoilMoisture, std::nullopt, Unit::Percent,
                           t0.addDays(1), Provenance::History };
        m.setReadings({ soil(40.0, t0), gap, soil(44.0, t0.addDays(2)) });
        // The absent reading is not a plottable point.
        QCOMPARE(m.points().size(), 2);
        QCOMPARE(m.rowCount(), 2);
    }

    void singlePointHasNonZeroSpans()
    {
        // A lone sample must NOT yield a zero-width axis — QtGraphs divides by the span
        // in updatePolish() and SIGFPEs otherwise (see HistoryChartScreen.qml).
        const QDateTime t0(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);
        SeriesModel m;
        m.setReadings({ soil(50.0, t0) });
        QCOMPARE(m.points().size(), 1);
        QVERIFY(m.tMax() > m.tMin());       // time axis has a real span
        QVERIFY(m.axisMax() > m.axisMin()); // value axis padded around the flat value
        const double mid = double(t0.toMSecsSinceEpoch());
        QVERIFY(m.tMin() < mid && mid < m.tMax()); // point sits inside the window
    }

    void snapsTimeBoundsToRoundStep()
    {
        // Readings at 13:47 and 19:13 (a ~5.4 h intraday span) snap the reported time bounds
        // OUT to whole hours, so the DateTimeAxis ticks land on the hour (ADR 0006 follow-up).
        const QDateTime a(QDate(2026, 1, 5), QTime(13, 47), QTimeZone::UTC);
        const QDateTime b(QDate(2026, 1, 5), QTime(19, 13), QTimeZone::UTC);
        SeriesModel m;
        m.setReadings({ soil(40.0, a), soil(45.0, b) });

        const QDateTime lo(QDate(2026, 1, 5), QTime(13, 0), QTimeZone::UTC); // floored
        const QDateTime hi(QDate(2026, 1, 5), QTime(20, 0), QTimeZone::UTC); // ceiled
        QCOMPARE(m.tMin(), double(lo.toMSecsSinceEpoch()));
        QCOMPARE(m.tMax(), double(hi.toMSecsSinceEpoch()));
        QCOMPARE(m.tMinDate(), lo);
        QCOMPARE(m.tMaxDate(), hi);

        // Points keep their RAW timestamps and sit inside the snapped window.
        QCOMPARE(m.points().first().x(), double(a.toMSecsSinceEpoch()));
        QVERIFY(m.tMin() <= m.points().first().x());
        QVERIFY(m.points().last().x() <= m.tMax());
    }

    void idealBandIsPublishedAndGrowsTheAxis()
    {
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        SeriesModel m;
        // Readings sit at 40..44; an ideal band of [20,60] must be visible, so the value
        // axis grows to include it.
        m.setReadings({ soil(40.0, t0), soil(44.0, t0.addDays(1)) },
                      CareRange{ Quantity::SoilMoisture, 20.0, 60.0 });
        QVERIFY(m.hasBand());
        QVERIFY(m.axisMin() <= 20.0);
        QVERIFY(m.axisMax() >= 60.0);
        QCOMPARE(m.bandMin(), 20.0);
        QCOMPARE(m.bandMax(), 60.0);
    }

    void oneSidedBandClampsToAxisEdge()
    {
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        SeriesModel m;
        // Only a minimum (ideal >= 30): the open top clamps to the axis max.
        m.setReadings({ soil(40.0, t0), soil(44.0, t0.addDays(1)) },
                      CareRange{ Quantity::SoilMoisture, 30.0, std::nullopt });
        QVERIFY(m.hasBand());
        QCOMPARE(m.bandMin(), 30.0);
        QCOMPARE(m.bandMax(), m.axisMax()); // open side fills to the edge
    }

    void noBandWhenRangeUnset()
    {
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        SeriesModel m;
        m.setReadings({ soil(40.0, t0) }, CareRange{ Quantity::SoilMoisture, std::nullopt,
                                                     std::nullopt });
        QVERIFY(!m.hasBand());
    }

    void emptySeriesHasSafeDefaults()
    {
        SeriesModel m;
        m.setReadings({});
        QVERIFY(m.empty());
        QCOMPARE(m.rowCount(), 0);
        QVERIFY(m.points().isEmpty());
        // No NaN/inf axis — safe defaults so a QML ValueAxis still binds.
        QVERIFY(qIsFinite(m.axisMin()) && qIsFinite(m.axisMax()));
        QVERIFY(m.axisMax() > m.axisMin());
        QVERIFY(m.tickInterval() > 0.0);
        QVERIFY(m.tMax() > m.tMin());
        // X-axis tick step stays positive and the format non-empty even with no data.
        QVERIFY(m.xTickInterval() > 0.0);
        QVERIFY(!m.xLabelFormat().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSeriesModel)
#include "test_seriesmodel.moc"
