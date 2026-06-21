// SPDX-License-Identifier: GPL-3.0-or-later
#include "seriesmodel.h"

#include <algorithm>
#include <limits>

namespace klr {

void SeriesModel::setReadings(const QList<Reading> &readings, const std::optional<CareRange> &band)
{
    beginResetModel();
    m_points.clear();

    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    double tLo = std::numeric_limits<double>::max();
    double tHi = std::numeric_limits<double>::lowest();
    for (const Reading &r : readings) {
        if (!r.value.has_value())
            continue; // a gap in the series — not a plottable point
        const double x = double(r.timestamp.toMSecsSinceEpoch());
        const double y = *r.value;
        m_points.append(QPointF(x, y));
        lo = std::min(lo, y);
        hi = std::max(hi, y);
        tLo = std::min(tLo, x);
        tHi = std::max(tHi, x);
    }

    // Fold the band's set bounds into the value range so the whole ideal band shows
    // alongside the readings (a one-sided bound only stretches its own side).
    m_hasBand = band.has_value() && band->isSet() && !m_points.isEmpty();
    if (m_hasBand) {
        if (band->min)
            lo = std::min(lo, *band->min);
        if (band->max)
            hi = std::max(hi, *band->max);
    }

    if (m_points.isEmpty()) {
        m_y = AxisRange{};            // 0..1, tick 1
        m_tMin = 0.0;
        m_tMax = 1.0;
        m_hasBand = false;
        m_bandMin = m_y.min;
        m_bandMax = m_y.max;
    } else {
        m_y = niceAxis(lo, hi);       // niceAxis guarantees min < max (pads a flat range)
        if (tHi > tLo) {
            m_tMin = tLo;
            m_tMax = tHi;
        } else {
            // A single sample (or all in one instant): centre it in a half-hour window so
            // the time axis has a real, non-zero span (QtGraphs divides by it — SIGFPE
            // otherwise; see HistoryChartScreen.qml).
            constexpr double kHalfHourMs = 1800000.0;
            m_tMin = tLo - kHalfHourMs;
            m_tMax = tLo + kHalfHourMs;
        }
        // Clamp the band to the (post-nice) axis; an open side fills to the axis edge.
        m_bandMin = m_hasBand && band->min ? std::max(*band->min, m_y.min) : m_y.min;
        m_bandMax = m_hasBand && band->max ? std::min(*band->max, m_y.max) : m_y.max;
    }

    // Calendar-snapped X ticks for the (possibly padded) time span — always a positive
    // interval, so QtGraphs' DateTimeAxis never divides by zero. Adopt the snapped bounds
    // as the reported tMin/tMax so DateTimeAxis.min/max align with the ticks (gridlines on
    // the hour/day boundary). The points keep their raw timestamps and stay inside the
    // outward-rounded window. The empty sentinel (0..1) is left as-is — the chart is gated
    // on non-empty, so its bounds never reach the axis.
    m_x = niceTimeAxis(m_tMin, m_tMax);
    if (!m_points.isEmpty()) {
        m_tMin = m_x.minMs;
        m_tMax = m_x.maxMs;
    }

    endResetModel();
    emit changed();
}

void SeriesModel::clear()
{
    setReadings({}, std::nullopt);
}

int SeriesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_points.size());
}

QVariant SeriesModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_points.size())
        return {};
    const QPointF &p = m_points.at(index.row());
    switch (role) {
    case XRole: return p.x();
    case YRole: return p.y();
    default:    return {};
    }
}

QHash<int, QByteArray> SeriesModel::roleNames() const
{
    return {
        { XRole, "x" },
        { YRole, "y" },
    };
}

} // namespace klr
