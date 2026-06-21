// SPDX-License-Identifier: GPL-3.0-or-later
#include "axis.h"

#include <algorithm>
#include <cmath>

namespace klr {

namespace {

// The classic "nice number" rounding (Heckbert): map x to the nearest 1/2/5x10^n,
// rounding down (round=true picks the closest "nice" step) or up (round=false widens
// the span). x is expected > 0.
double niceNum(double x, bool round)
{
    const double expv = std::floor(std::log10(x));
    const double f = x / std::pow(10.0, expv); // in [1, 10)
    double nf;
    if (round)
        nf = f < 1.5 ? 1.0 : f < 3.0 ? 2.0 : f < 7.0 ? 5.0 : 10.0;
    else
        nf = f <= 1.0 ? 1.0 : f <= 2.0 ? 2.0 : f <= 5.0 ? 5.0 : 10.0;
    return nf * std::pow(10.0, expv);
}

} // namespace

TimeAxis niceTimeAxis(double tMinMs, double tMaxMs, int targetTicks)
{
    targetTicks = std::max(2, targetTicks);

    constexpr double kHour = 3'600'000.0; // ms
    constexpr double kDay = 86'400'000.0; // ms

    if (tMaxMs < tMinMs)
        std::swap(tMinMs, tMaxMs);
    double span = tMaxMs - tMinMs;
    if (!(span > 0.0)) {
        tMaxMs = tMinMs + kHour; // degenerate / single instant: a one-hour window
        span = kHour;
    }

    // The round "calendar" steps we snap to. Hour/day rungs are exact multiples from the
    // UNIX epoch (00:00 UTC); the week/month rungs (7d / ~30d) are approximate but only
    // ever carry coarse "MMM d" / "MMM yyyy" labels, so the approximation is invisible.
    const double kLadder[] = { kHour,      3.0 * kHour,  6.0 * kHour, 12.0 * kHour,
                               kDay,       7.0 * kDay,   30.0 * kDay };

    // Pick the rung closest (in ratio / log space, like niceNum) to the step that would
    // give about targetTicks-1 divisions.
    const double ideal = span / double(targetTicks - 1);
    double step = kLadder[0];
    double bestRatio = (kLadder[0] >= ideal) ? kLadder[0] / ideal : ideal / kLadder[0];
    for (double rung : kLadder) {
        const double ratio = (rung >= ideal) ? rung / ideal : ideal / rung;
        if (ratio < bestRatio) {
            bestRatio = ratio;
            step = rung;
        }
    }

    // Snap the bounds OUT to a multiple of the step (so the data stays inside), then count
    // the divisions. Coarsen (double the step) if the count would breach QtGraphs' clamp.
    double snappedMin = std::floor(tMinMs / step) * step;
    double snappedMax = std::ceil(tMaxMs / step) * step;
    double count = std::round((snappedMax - snappedMin) / step);
    while (count > 100.0) {
        step *= 2.0;
        snappedMin = std::floor(tMinMs / step) * step;
        snappedMax = std::ceil(tMaxMs / step) * step;
        count = std::round((snappedMax - snappedMin) / step);
    }
    if (count < 1.0)
        count = 1.0;

    // Label scale follows the snapped span: clock for intraday, calendar date up to a few
    // months, month+year beyond (so labels stay distinguishable and unambiguous).
    const double snappedSpan = snappedMax - snappedMin;
    QString fmt;
    if (snappedSpan <= 2.0 * kDay)
        fmt = QStringLiteral("HH:mm");
    else if (snappedSpan <= 120.0 * kDay)
        fmt = QStringLiteral("MMM d");
    else
        fmt = QStringLiteral("MMM yyyy");

    return { count, fmt, snappedMin, snappedMax };
}

AxisRange niceAxis(double lo, double hi, int targetTicks)
{
    targetTicks = std::max(2, targetTicks);

    if (hi < lo)
        std::swap(lo, hi);

    // Flat range: pad around the value so a constant series is still visible.
    if (hi <= lo) {
        const double pad = std::max(1.0, std::abs(lo) * 0.1);
        lo -= pad;
        hi += pad;
    }

    const double range = niceNum(hi - lo, false);
    const double tick = niceNum(range / (targetTicks - 1), true);
    const double niceMin = std::floor(lo / tick) * tick;
    const double niceMax = std::ceil(hi / tick) * tick;
    return { niceMin, niceMax, tick };
}

} // namespace klr
