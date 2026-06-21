// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// "Nice" value-axis computation — pure, unit-tested. QtGraphs does NOT auto-range its
// axes, so the chart view-model must publish min/max/tickInterval itself (see
// ../../docs/adr/0006-history-charts-import.md).
// A tested helper for nice axis bounds (WatchFlower computed this inline in applyTimeAxis).

#include <QtCore/QString>

namespace klr {

struct AxisRange {
    double min = 0.0;
    double max = 1.0;
    double tickInterval = 1.0;
};

// Round the data range [lo, hi] out to human-friendly 1/2/5x10^n bounds with about
// `targetTicks` gridlines. A flat range (lo == hi, or hi < lo) is padded around the
// value so the series is still visible. targetTicks is clamped to >= 2.
AxisRange niceAxis(double lo, double hi, int targetTicks = 5);

// A "nice" DateTimeAxis for a QtGraphs chart spanning [tMinMs, tMaxMs] (ms since epoch):
// bounds SNAPPED out to a round calendar step, the matching division count, and a
// span-appropriate label format. The time-axis analogue of niceAxis (the Y axis already
// snapped; this brings the X axis up to the same standard — ADR 0006 follow-up).
//
// IMPORTANT: a DateTimeAxis's `tickInterval` is NOT a time spacing — QtGraphs clamps it
// to [0, 100] and uses it as the NUMBER of divisions (see axisrenderer.cpp updateAxis()).
// Feeding it a millisecond span (the obvious mistake) clamps to 100 and floods the axis.
// So we instead choose a round step from a ladder (1h/3h/6h/12h/1d/1w/~1mo) sized to the
// span, FLOOR `minMs` / CEIL `maxMs` to a multiple of that step, and report the resulting
// division count. The caller binds `minMs`/`maxMs` to DateTimeAxis.min/max so gridlines
// land on the hour/day boundary (e.g. "13:00", not "13:47"). Steps are multiples from the
// UNIX epoch (00:00 UTC), so hour/day ticks are exact in UTC; week/month rungs approximate.
// The count is coarsened (step doubled) if it would exceed QtGraphs' 100 clamp. A
// degenerate/zero span is treated as a one-hour window. targetTicks is clamped to >= 2.
struct TimeAxis {
    double tickInterval = 4.0; // value for DateTimeAxis.tickInterval — a DIVISION COUNT
    QString labelFormat;       // Qt QDateTime format string
    double minMs = 0.0;        // snapped lower bound — bind to DateTimeAxis.min
    double maxMs = 0.0;        // snapped upper bound — bind to DateTimeAxis.max
};
TimeAxis niceTimeAxis(double tMinMs, double tMaxMs, int targetTicks = 5);

} // namespace klr
