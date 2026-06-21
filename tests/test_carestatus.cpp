// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "carestatus.h"

#include <array>

using namespace klr;

// The pure care-status judgment: evaluate one reading against a range, and roll
// up the per-quantity verdicts to one plant-level level. Literal inputs only — no DB,
// no clock, no QML.
class TestCareStatus : public QObject {
    Q_OBJECT

    static CareRange range(std::optional<double> min, std::optional<double> max)
    {
        return CareRange{ Quantity::SoilMoisture, min, max };
    }

private slots:
    void absentValueIsUnknown()
    {
        QCOMPARE(evaluate(std::nullopt, range(20.0, 60.0)), CareStatus::Unknown);
    }

    void unsetRangeIsUnknown()
    {
        // A value present but no threshold to judge against.
        QCOMPARE(evaluate(42.0, range(std::nullopt, std::nullopt)), CareStatus::Unknown);
        QVERIFY(!range(std::nullopt, std::nullopt).isSet());
    }

    void classifiesAgainstBothBounds()
    {
        const CareRange r = range(20.0, 60.0);
        QCOMPARE(evaluate(10.0, r), CareStatus::TooLow);
        QCOMPARE(evaluate(40.0, r), CareStatus::Ideal);
        QCOMPARE(evaluate(80.0, r), CareStatus::TooHigh);
    }

    void boundsAreInclusive()
    {
        const CareRange r = range(20.0, 60.0);
        QCOMPARE(evaluate(20.0, r), CareStatus::Ideal); // exactly min -> not TooLow
        QCOMPARE(evaluate(60.0, r), CareStatus::Ideal); // exactly max -> not TooHigh
    }

    void oneSidedRanges()
    {
        // Only a minimum: anything at/above it is Ideal, below is TooLow.
        const CareRange minOnly = range(20.0, std::nullopt);
        QCOMPARE(evaluate(5.0, minOnly), CareStatus::TooLow);
        QCOMPARE(evaluate(99.0, minOnly), CareStatus::Ideal);
        // Only a maximum.
        const CareRange maxOnly = range(std::nullopt, 60.0);
        QCOMPARE(evaluate(99.0, maxOnly), CareStatus::TooHigh);
        QCOMPARE(evaluate(5.0, maxOnly), CareStatus::Ideal);
    }

    void rollupWorstOfWins()
    {
        const std::array attention{ CareStatus::Ideal, CareStatus::TooLow, CareStatus::Ideal };
        QCOMPARE(rollup(attention), CareLevel::Attention);

        const std::array high{ CareStatus::Ideal, CareStatus::TooHigh };
        QCOMPARE(rollup(high), CareLevel::Attention);

        const std::array good{ CareStatus::Ideal, CareStatus::Unknown };
        QCOMPARE(rollup(good), CareLevel::Good);

        const std::array nothing{ CareStatus::Unknown, CareStatus::Unknown };
        QCOMPARE(rollup(nothing), CareLevel::Unknown);

        QCOMPARE(rollup(std::span<const CareStatus>{}), CareLevel::Unknown);
    }

    void isAlertingIsOutOfRangeEitherWay()
    {
        QVERIFY(isAlerting(CareStatus::TooLow));
        QVERIFY(isAlerting(CareStatus::TooHigh));
        QVERIFY(!isAlerting(CareStatus::Ideal));   // in range — nothing to say
        QVERIFY(!isAlerting(CareStatus::Unknown)); // nothing measured/judged
    }

    void shouldNotifyOnlyOnTransitionIntoAlerting()
    {
        // Fire only when crossing INTO an alerting state — the debounce that turns a stream
        // of identical TooLow advertisements into one notification.
        QVERIFY(shouldNotify(CareStatus::Ideal, CareStatus::TooLow));
        QVERIFY(shouldNotify(CareStatus::Unknown, CareStatus::TooHigh)); // first-seen dry plant
        QVERIFY(shouldNotify(CareStatus::Ideal, CareStatus::TooHigh));

        // Already alerting — staying bad (or moving between bad states) does NOT re-fire.
        QVERIFY(!shouldNotify(CareStatus::TooLow, CareStatus::TooLow));
        QVERIFY(!shouldNotify(CareStatus::TooLow, CareStatus::TooHigh));

        // Leaving an alerting state is silent (recovery notifications deferred, ADR 0016).
        QVERIFY(!shouldNotify(CareStatus::TooLow, CareStatus::Ideal));
        QVERIFY(!shouldNotify(CareStatus::TooHigh, CareStatus::Unknown));

        // Neutral → neutral never fires.
        QVERIFY(!shouldNotify(CareStatus::Unknown, CareStatus::Ideal));
        QVERIFY(!shouldNotify(CareStatus::Ideal, CareStatus::Ideal));
    }

    void temperatureJudgedOnRecentExtremes()
    {
        QVERIFY(judgedOnRecentExtremes(Quantity::AirTemperature));
        QVERIFY(judgedOnRecentExtremes(Quantity::SoilTemperature));
        QVERIFY(!judgedOnRecentExtremes(Quantity::SoilMoisture));
        QVERIFY(!judgedOnRecentExtremes(Quantity::Illuminance));
        QVERIFY(kExtremesWindowMs > 0);
    }

    void extremesOfAndEvaluate()
    {
        const QDateTime t0(QDate(2026, 1, 1), QTime(3, 0), QTimeZone::UTC);
        const std::array readings{
            Reading{ Quantity::AirTemperature, 4.0, Unit::DegreeCelsius, t0, Provenance::History },
            Reading{ Quantity::AirTemperature, std::nullopt, Unit::DegreeCelsius, t0.addSecs(3600),
                     Provenance::History }, // a gap — ignored
            Reading{ Quantity::AirTemperature, 20.0, Unit::DegreeCelsius, t0.addSecs(8 * 3600),
                     Provenance::History }, // recovered
        };
        const Extremes e = extremesOf(readings);
        QCOMPARE(e.min, std::optional<double>(4.0));
        QCOMPARE(e.max, std::optional<double>(20.0));

        const CareRange r{ Quantity::AirTemperature, 10.0, 30.0 };
        // A cold dip anywhere in the window is flagged even though it recovered to 20.
        QCOMPARE(evaluateExtremes(e, r), CareStatus::TooLow);
        QCOMPARE(evaluateExtremes(Extremes{ 18.0, 22.0 }, r), CareStatus::Ideal);
        QCOMPARE(evaluateExtremes(Extremes{ 18.0, 35.0 }, r), CareStatus::TooHigh);
        QCOMPARE(evaluateExtremes(Extremes{ 4.0, 35.0 }, r), CareStatus::TooLow); // both → low wins
        QCOMPARE(evaluateExtremes(Extremes{}, r), CareStatus::Unknown);           // no data
    }

    void lightJudgedOnDailyIntegral()
    {
        QVERIFY(judgedOnDailyIntegral(Quantity::Illuminance));
        QVERIFY(judgedOnDailyIntegral(Quantity::Ppfd));
        QVERIFY(!judgedOnDailyIntegral(Quantity::SoilMoisture));
        QVERIFY(!judgedOnDailyIntegral(Quantity::AirTemperature));
        QVERIFY(kDliWindowDays >= 1); // a sane, tunable window
    }

    void dliOfTrapezoidIntegratesTheDay()
    {
        const QDateTime dayStart(QDate(2026, 1, 5), QTime(0, 0), QTimeZone::UTC);
        // Two PPFD samples 12 h apart at 100 µmol·m⁻²·s⁻¹ → ½·(100+100)·43200 s = 4.32e6 µmol
        // → 4320 mmol·m⁻²·day⁻¹.
        const std::array day{
            Reading{ Quantity::Ppfd, 100.0, Unit::Micromole, dayStart.addSecs(6 * 3600),
                     Provenance::History },
            Reading{ Quantity::Ppfd, 100.0, Unit::Micromole, dayStart.addSecs(18 * 3600),
                     Provenance::History },
        };
        const std::optional<double> dose = dliOf(day, dayStart);
        QVERIFY(dose.has_value());
        QVERIFY(qFuzzyCompare(*dose, 4320.0));
    }

    void dliOfNeedsTwoInWindowSamplesAndIgnoresOtherDays()
    {
        const QDateTime dayStart(QDate(2026, 1, 5), QTime(0, 0), QTimeZone::UTC);
        const std::array one{
            Reading{ Quantity::Ppfd, 100.0, Unit::Micromole, dayStart.addSecs(6 * 3600),
                     Provenance::History },
        };
        QVERIFY(!dliOf(one, dayStart).has_value()); // a single sample can't integrate a dose

        // Samples in the neighbouring days are excluded — only the in-window pair counts.
        const std::array spanning{
            Reading{ Quantity::Ppfd, 9999.0, Unit::Micromole, dayStart.addSecs(-3600),
                     Provenance::History }, // yesterday
            Reading{ Quantity::Ppfd, 100.0, Unit::Micromole, dayStart.addSecs(6 * 3600),
                     Provenance::History },
            Reading{ Quantity::Ppfd, 100.0, Unit::Micromole, dayStart.addSecs(18 * 3600),
                     Provenance::History },
            Reading{ Quantity::Ppfd, 9999.0, Unit::Micromole, dayStart.addDays(1).addSecs(3600),
                     Provenance::History }, // tomorrow
        };
        QVERIFY(qFuzzyCompare(*dliOf(spanning, dayStart), 4320.0));
    }

    void dliOfConvertsLuxToPpfd()
    {
        const QDateTime dayStart(QDate(2026, 1, 5), QTime(0, 0), QTimeZone::UTC);
        // lux → PPFD via ×0.0185; 100 µmol ≡ 100/0.0185 lux, so the dose matches the µmol case.
        const double lux = 100.0 / 0.0185;
        const std::array day{
            Reading{ Quantity::Illuminance, lux, Unit::Lux, dayStart.addSecs(6 * 3600),
                     Provenance::History },
            Reading{ Quantity::Illuminance, lux, Unit::Lux, dayStart.addSecs(18 * 3600),
                     Provenance::History },
        };
        QVERIFY(qAbs(*dliOf(day, dayStart) - 4320.0) < 1.0); // floating-point round-trip slack
    }

    void meanDailyLightIntegralAveragesCompletedDays()
    {
        const QDateTime now(QDate(2026, 1, 10), QTime(12, 0)); // local "today", still in progress
        QList<Reading> r;
        auto add = [&r](const QDate &d, double ppfd) { // a 12-h-apart pair → ppfd·43.2 mmol
            r.append(Reading{ Quantity::Ppfd, ppfd, Unit::Micromole, QDateTime(d, QTime(6, 0)),
                              Provenance::History });
            r.append(Reading{ Quantity::Ppfd, ppfd, Unit::Micromole, QDateTime(d, QTime(18, 0)),
                              Provenance::History });
        };
        add(QDate(2026, 1, 7), 300.0);  // 12960
        add(QDate(2026, 1, 8), 200.0);  // 8640
        add(QDate(2026, 1, 9), 100.0);  // 4320
        add(QDate(2026, 1, 10), 9999.0); // today — in progress, must be excluded
        const std::optional<double> mean =
            meanDailyLightIntegral(std::span<const Reading>(r.constData(), r.size()), now, 3);
        QVERIFY(mean.has_value());
        QVERIFY(qFuzzyCompare(*mean, 8640.0)); // (12960 + 8640 + 4320) / 3
    }

    void meanDailyLightIntegralWithholdsUntilAFullDay()
    {
        const QDateTime now(QDate(2026, 1, 10), QTime(12, 0));
        // Only the in-progress day has data → no completed day → Unknown (partial-day guard).
        QList<Reading> r{
            Reading{ Quantity::Ppfd, 500.0, Unit::Micromole, QDateTime(QDate(2026, 1, 10), QTime(6, 0)),
                     Provenance::History },
            Reading{ Quantity::Ppfd, 500.0, Unit::Micromole, QDateTime(QDate(2026, 1, 10), QTime(11, 0)),
                     Provenance::History },
        };
        QVERIFY(!meanDailyLightIntegral(std::span<const Reading>(r.constData(), r.size()), now, 3)
                     .has_value());
    }

    void evaluateDliClassifiesDose()
    {
        const CareRange dli{ Quantity::Dli, 3500.0, 30000.0 };
        QCOMPARE(evaluateDli(std::nullopt, dli), CareStatus::Unknown); // no full day yet
        QCOMPARE(evaluateDli(1000.0, dli), CareStatus::TooLow);
        QCOMPARE(evaluateDli(10000.0, dli), CareStatus::Ideal);
        // Min-only: a dose above max is "ample light", NOT TooHigh — the catalog max is the
        // top of an *ideal* band, not a tolerance ceiling (see evaluateDli rationale).
        QCOMPARE(evaluateDli(50000.0, dli), CareStatus::Ideal);
    }

    void statusForReadingDispatchesPerQuantity()
    {
        // The shared router picks the evaluator by quantity and consults the recent window
        // for the dose/extremes quantities — the dispatch the list & care models share.
        const QDateTime now(QDate(2026, 1, 6), QTime(12, 0)); // local; Jan 5 is a completed day
        const std::array ranges{
            CareRange{ Quantity::SoilMoisture, 20.0, 60.0 },
            CareRange{ Quantity::AirTemperature, 10.0, 30.0 },
            CareRange{ Quantity::Dli, 3500.0, 30000.0 }, // light is judged on the DOSE now
        };
        const std::span<const CareRange> span(ranges.data(), ranges.size());

        // The window the models would fetch from the repository, keyed by quantity.
        const ReadingWindowFn window = [&](Quantity q, const QDateTime &,
                                           const QDateTime &) -> QList<Reading> {
            if (q == Quantity::Illuminance) { // a sustained-bright completed day → high dose
                const QDate d(2026, 1, 5);
                return { Reading{ q, 80000.0, Unit::Lux, QDateTime(d, QTime(6, 0)),
                                  Provenance::History },
                         Reading{ q, 80000.0, Unit::Lux, QDateTime(d, QTime(18, 0)),
                                  Provenance::History } };
            }
            if (q == Quantity::AirTemperature) // an overnight cold dip that has since recovered
                return { Reading{ q, 4.0, Unit::DegreeCelsius, now.addSecs(-3600),
                                  Provenance::History } };
            return {};
        };

        // Soil → current value (no window): 10 % is below the 20–60 range.
        const Reading soil{ Quantity::SoilMoisture, 10.0, Unit::Percent, now, Provenance::Live };
        QCOMPARE(statusForReading(soil, span, now, window), CareStatus::TooLow);

        // Light → DAILY DOSE judged against the Dli range (NOT the Illuminance lux range,
        // which is absent here): a full day at 80000 lux integrates to ~64000 mmol > 30000 —
        // but light is min-only, so a dose above max reads Ideal ("ample"), never TooHigh.
        const Reading lightNow{ Quantity::Illuminance, 100.0, Unit::Lux, now, Provenance::Live };
        QCOMPARE(statusForReading(lightNow, span, now, window), CareStatus::Ideal);

        // Temperature → recent EXTREMES: 20 °C now is Ideal, but the window's 4 °C dip flags TooLow.
        const Reading warmNow{ Quantity::AirTemperature, 20.0, Unit::DegreeCelsius, now,
                               Provenance::Live };
        QCOMPARE(statusForReading(warmNow, span, now, window), CareStatus::TooLow);

        // No range for the quantity → Unknown (the rollup then ignores it).
        const Reading co2{ Quantity::Co2, 800.0, Unit::Ppm, now, Provenance::Live };
        QCOMPARE(statusForReading(co2, span, now, window), CareStatus::Unknown);

        // A pre-computed Dli reading (the care tab's "Daily light" row) is judged on its own
        // value, min-only: above max is Ideal ("ample"), below min is TooLow. Not the default
        // evaluate() — so the row's pill matches the min-only rollup and never shows TooHigh.
        const Reading doseHigh{ Quantity::Dli, 64000.0, Unit::None, now, Provenance::History };
        QCOMPARE(statusForReading(doseHigh, span, now, window), CareStatus::Ideal);
        const Reading doseLow{ Quantity::Dli, 1000.0, Unit::None, now, Provenance::History };
        QCOMPARE(statusForReading(doseLow, span, now, window), CareStatus::TooLow);
    }

    void rangeForFindsByQuantity()
    {
        const std::array ranges{
            CareRange{ Quantity::SoilMoisture, 20.0, 60.0 },
            CareRange{ Quantity::AirTemperature, 15.0, 30.0 },
        };
        const std::optional<CareRange> t = rangeFor(ranges, Quantity::AirTemperature);
        QVERIFY(t.has_value());
        QCOMPARE(t->min, std::optional<double>(15.0));
        QVERIFY(!rangeFor(ranges, Quantity::Illuminance).has_value());
    }
};

QTEST_GUILESS_MAIN(TestCareStatus)
#include "test_carestatus.moc"
