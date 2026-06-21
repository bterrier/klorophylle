// SPDX-License-Identifier: GPL-3.0-or-later
#include "carestatus.h"

#include "units.h" // convert(): lux → PPFD for the DLI integration

#include <QtCore/QDateTime>

#include <algorithm>

namespace klr {

CareStatus evaluate(std::optional<double> value, const CareRange &range)
{
    if (!value || !range.isSet())
        return CareStatus::Unknown;
    if (range.min && *value < *range.min)
        return CareStatus::TooLow;
    if (range.max && *value > *range.max)
        return CareStatus::TooHigh;
    return CareStatus::Ideal;
}

bool isAlerting(CareStatus s)
{
    return s == CareStatus::TooLow || s == CareStatus::TooHigh;
}

bool shouldNotify(CareStatus previous, CareStatus current)
{
    return isAlerting(current) && !isAlerting(previous);
}

bool judgedOnRecentExtremes(Quantity q)
{
    return q == Quantity::AirTemperature || q == Quantity::SoilTemperature;
}

Extremes extremesOf(std::span<const Reading> readings)
{
    Extremes e;
    for (const Reading &r : readings) {
        if (!r.value)
            continue;
        e.min = e.min ? std::min(*e.min, *r.value) : *r.value;
        e.max = e.max ? std::max(*e.max, *r.value) : *r.value;
    }
    return e;
}

CareStatus evaluateExtremes(const Extremes &extremes, const CareRange &range)
{
    if (!range.isSet())
        return CareStatus::Unknown;
    // A breach of either bound anywhere in the window is flagged; low wins if both.
    if (evaluate(extremes.min, range) == CareStatus::TooLow)
        return CareStatus::TooLow;
    if (evaluate(extremes.max, range) == CareStatus::TooHigh)
        return CareStatus::TooHigh;
    return extremes.min || extremes.max ? CareStatus::Ideal : CareStatus::Unknown;
}

bool judgedOnDailyIntegral(Quantity q)
{
    return q == Quantity::Illuminance || q == Quantity::Ppfd;
}

std::optional<double> dliOf(std::span<const Reading> readings, const QDateTime &dayStart)
{
    const QDateTime dayEnd = dayStart.addDays(1);
    double micromolSeconds = 0.0;
    int inWindow = 0;
    std::optional<qint64> prevMs;
    std::optional<double> prevPpfd;
    for (const Reading &r : readings) {
        if (!r.value || r.timestamp < dayStart || r.timestamp >= dayEnd)
            continue; // absent, or outside this day — out-of-window samples sit at the ends
        ++inWindow;
        // lux → PPFD (µmol·m⁻²·s⁻¹) via the documented daylight factor; a µmol reading
        // (Quantity::Ppfd) passes through unchanged (convert is identity when from == to).
        const double ppfd = convert(*r.value, r.unit, Unit::Micromole);
        const qint64 ms = r.timestamp.toMSecsSinceEpoch();
        if (prevMs && prevPpfd) {
            const double dtSec = double(ms - *prevMs) / 1000.0;
            micromolSeconds += 0.5 * (*prevPpfd + ppfd) * dtSec; // trapezoid
        }
        prevMs = ms;
        prevPpfd = ppfd;
    }
    if (inWindow < 2)
        return std::nullopt; // a single in-window sample cannot integrate a dose
    return micromolSeconds / 1000.0; // µmol·m⁻²·day⁻¹ → mmol·m⁻²·day⁻¹
}

std::optional<double> meanDailyLightIntegral(std::span<const Reading> readings,
                                             const QDateTime &now, int days)
{
    // A plant's daily dose is a solar/human-day concept, so the day boundary is LOCAL
    // midnight. `now`'s own local day is excluded — it is still in progress.
    const QDateTime localNow = now.toLocalTime();
    const QDateTime todayStart(localNow.date(), QTime(0, 0)); // local time
    double sum = 0.0;
    int counted = 0;
    for (int k = 1; k <= days; ++k) {
        if (const std::optional<double> dli = dliOf(readings, todayStart.addDays(-k))) {
            sum += *dli;
            ++counted;
        }
    }
    if (counted == 0)
        return std::nullopt; // no completed day with data yet — withhold the verdict
    return sum / counted;
}

CareStatus evaluateDli(std::optional<double> dose, const CareRange &range)
{
    // Min-only: light is judged for "enough?", never "too much?". The catalog's mmol max is
    // the top of the plant's *ideal* band, not a tolerance ceiling — for the many outdoor
    // species in the catalog it sits far below physical outdoor DLI (4–8 vs 30–60 mol·m⁻²·d⁻¹
    // in full sun), so a dose above it means "ample light", not harm. Judging a ceiling that
    // every sun-exposed plant clears was the "too high everywhere" bug. A dose above max
    // therefore reads Ideal; only below min (too little daily light) flags. nullopt → Unknown.
    const CareStatus s = evaluate(dose, range); // nullopt dose → Unknown (no full day yet)
    return s == CareStatus::TooHigh ? CareStatus::Ideal : s;
}

CareStatus statusForReading(const Reading &current, std::span<const CareRange> ranges,
                            const QDateTime &now, const ReadingWindowFn &window)
{
    // Light is judged on its accumulated Daily Light Integral over recent completed days,
    // against the plant's Dli range — NOT the lux range, which survives only as a
    // display range for the metric bar. The catalog's lux max is a *sustained* environment
    // bound, so testing a daily peak against it over-flagged any plant that ever caught the
    // sun. No dose range, or no full day observed yet ⇒ Unknown.
    if (judgedOnDailyIntegral(current.quantity)) {
        const std::optional<CareRange> dliRange = rangeFor(ranges, Quantity::Dli);
        if (!dliRange)
            return CareStatus::Unknown;
        const QDateTime from = now.addDays(-(kDliWindowDays + 1)); // cover the oldest full day
        const QList<Reading> s = window(current.quantity, from, now);
        return evaluateDli(
            meanDailyLightIntegral(std::span<const Reading>(s.constData(), s.size()), now),
            *dliRange);
    }
    // A Quantity::Dli reading is a pre-computed dose (the care tab's synthesized "Daily light"
    // row): judge its value directly, min-only, so the row's pill matches the rollup and a
    // dose above max never shows TooHigh. Goes through evaluateDli, NOT the default evaluate().
    if (current.quantity == Quantity::Dli) {
        const std::optional<CareRange> dliRange = rangeFor(ranges, Quantity::Dli);
        return dliRange ? evaluateDli(current.value, *dliRange) : CareStatus::Unknown;
    }
    // Temperature is judged on its recent EXTREMES (so an overnight cold snap still alerts in
    // the morning); everything else on its current value (ADR 0009). The current reading is
    // folded in, so a single value is still judged when history is empty (a just-attached
    // sensor whose first sample predates the binding window).
    const std::optional<CareRange> range = rangeFor(ranges, current.quantity);
    if (!range)
        return CareStatus::Unknown;
    if (judgedOnRecentExtremes(current.quantity)) {
        QList<Reading> s = window(current.quantity, now.addMSecs(-kExtremesWindowMs), now);
        s.append(current);
        return evaluateExtremes(extremesOf(std::span<const Reading>(s.constData(), s.size())),
                                *range);
    }
    return evaluate(current.value, *range);
}

CareLevel rollup(std::span<const CareStatus> statuses)
{
    bool anyIdeal = false;
    for (const CareStatus s : statuses) {
        if (s == CareStatus::TooLow || s == CareStatus::TooHigh)
            return CareLevel::Attention; // worst-of: one out-of-range value dominates
        if (s == CareStatus::Ideal)
            anyIdeal = true;
    }
    return anyIdeal ? CareLevel::Good : CareLevel::Unknown;
}

std::optional<CareRange> rangeFor(std::span<const CareRange> ranges, Quantity q)
{
    for (const CareRange &r : ranges) {
        if (r.quantity == q)
            return r;
    }
    return std::nullopt;
}

} // namespace klr
