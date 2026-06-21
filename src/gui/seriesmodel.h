// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "axis.h"
#include "carestatus.h" // CareRange (ideal-range band)
#include "reading.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QDateTime>
#include <QtCore/QList>
#include <QtCore/QPointF>
#include <QtCore/QTimeZone>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace klr {

// The chart view-model: one quantity's history as points + a pre-computed "nice" axis.
// QtGraphs does NOT auto-range, so this publishes axisMin/axisMax/tickInterval (value)
// and tMin/tMax (time, ms-since-epoch as double) for the QML ValueAxes to bind to. It is
// FILLED in C++ (setReadings) and exposes the points as a QList<QPointF> the QML
// LineSeries replaces into — C++ never reaches into a QML-created series.
// Also a QAbstractListModel (x/y roles) for non-chart consumers/tests.
class SeriesModel : public QAbstractListModel { // not 'final': QML_ELEMENT subclasses it
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<QPointF> points READ points NOTIFY changed)
    Q_PROPERTY(double axisMin READ axisMin NOTIFY changed)
    Q_PROPERTY(double axisMax READ axisMax NOTIFY changed)
    Q_PROPERTY(double tickInterval READ tickInterval NOTIFY changed)
    Q_PROPERTY(double tMin READ tMin NOTIFY changed)
    Q_PROPERTY(double tMax READ tMax NOTIFY changed)
    // The same time bounds as QDateTime, for binding a QtGraphs DateTimeAxis min/max.
    Q_PROPERTY(QDateTime tMinDate READ tMinDate NOTIFY changed)
    Q_PROPERTY(QDateTime tMaxDate READ tMaxDate NOTIFY changed)
    // The value to bind to a QtGraphs DateTimeAxis.tickInterval (a DIVISION COUNT, not a
    // ms spacing — QtGraphs clamps it to [0,100]) + the span-appropriate label format,
    // both from niceTimeAxis. The QML binds these so the X axis shows a handful of
    // readable ticks instead of the ~100 the old ms-span value clamped to.
    Q_PROPERTY(double xTickInterval READ xTickInterval NOTIFY changed)
    Q_PROPERTY(QString xLabelFormat READ xLabelFormat NOTIFY changed)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)
    // The plant's ideal-range band for this quantity, in the SAME unit as the
    // points. hasBand is false when the plant has no threshold for the quantity (then
    // bandMin/bandMax are the axis edges and QML draws nothing). A one-sided range
    // clamps its open edge to the axis, so the shaded region still reads as
    // "ideal above/below". The value axis is grown to include the band.
    Q_PROPERTY(bool hasBand READ hasBand NOTIFY changed)
    Q_PROPERTY(double bandMin READ bandMin NOTIFY changed)
    Q_PROPERTY(double bandMax READ bandMax NOTIFY changed)

public:
    enum Role {
        XRole = Qt::UserRole + 1, // time, ms since epoch (double)
        YRole,                    // value
    };

    explicit SeriesModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    // Replace the series with `readings` (absent values are skipped); recomputes the
    // points + axis ranges and emits changed(). An optional ideal-range `band` (already
    // in the readings' unit) is published for the chart and folded into the value axis
    // so it is always visible.
    void setReadings(const QList<Reading> &readings,
                     const std::optional<CareRange> &band = std::nullopt);
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QList<QPointF> points() const { return m_points; }
    double axisMin() const { return m_y.min; }
    double axisMax() const { return m_y.max; }
    double tickInterval() const { return m_y.tickInterval; }
    double tMin() const { return m_tMin; }
    double tMax() const { return m_tMax; }
    QDateTime tMinDate() const { return QDateTime::fromMSecsSinceEpoch(qint64(m_tMin), QTimeZone::UTC); }
    QDateTime tMaxDate() const { return QDateTime::fromMSecsSinceEpoch(qint64(m_tMax), QTimeZone::UTC); }
    double xTickInterval() const { return m_x.tickInterval; }
    QString xLabelFormat() const { return m_x.labelFormat; }
    bool empty() const { return m_points.isEmpty(); }
    bool hasBand() const { return m_hasBand; }
    double bandMin() const { return m_bandMin; }
    double bandMax() const { return m_bandMax; }

signals:
    void changed();

private:
    QList<QPointF> m_points;
    AxisRange m_y;          // value-axis range (default 0..1, tick 1)
    double m_tMin = 0.0;    // time-axis bounds (ms since epoch)
    double m_tMax = 1.0;
    TimeAxis m_x;           // time-axis tick spacing + label format (niceTimeAxis)
    bool m_hasBand = false;  // an ideal-range band is set for this series
    double m_bandMin = 0.0;  // band edges, clamped to the axis (open side -> axis edge)
    double m_bandMax = 1.0;
};

} // namespace klr
