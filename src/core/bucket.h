// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reading.h"

#include <QtCore/QHash>
#include <QtCore/qglobal.h>
#include <optional>

// Time-bucketing for stored history + the write-cadence gate — pure, clock-injected,
// unit-tested. WatchFlower used TWO notions of time: the history PK rounded the
// timestamp to a bucket, while a separate rolling-60-minute gate decided WHEN to write,
// and the two drifted apart so a bucket was occasionally skipped. Here the dedup key and the gate share one clock, so that cannot
// happen. See ../../docs/adr/0006-history-charts-import.md.
namespace klr {

// One hour in ms — the default history bucket. Half-hourly etc. is a parameter, not a
// fork: pass a different bucketMs.
inline constexpr qint64 kHourMs = 3'600'000;
inline constexpr qint64 kBucketMs = kHourMs;

// Floor a timestamp to its bucket boundary. Epoch ms are non-negative, so this is a
// plain floor; bucketMs <= 0 is treated as "no bucketing" (the timestamp itself).
constexpr qint64 bucketStartMs(qint64 tsMs, qint64 bucketMs = kBucketMs)
{
    if (bucketMs <= 0)
        return tsMs;
    return tsMs - (tsMs % bucketMs);
}

// Per-series write-cadence gate: collapse the flood of repeat advertisements (~1/s)
// into one DB write per (bucket, value). admit() returns true the first time a series
// enters a new bucket OR its value changes, and false for an identical repeat within the
// same bucket — so the live "current" display stays fresh on a real change yet the DB is
// not hammered by re-broadcasts of the same value. Because admit() buckets the same
// `nowMs` it gates on, every bucket that receives a sample is admitted at least once — a
// bucket can never be skipped (the multi-day skip/dup regression). A pure value object: the caller
// supplies the time (the reading's own timestamp, which is clock-derived in the live path).
class WriteCadenceGate {
public:
    explicit WriteCadenceGate(qint64 bucketMs = kBucketMs) : m_bucketMs(bucketMs) {}

    // True iff (sensorKey, quantity) should be written now: a new bucket, or a value
    // different from the last admitted sample of the same bucket. Records the decision.
    bool admit(const QString &sensorKey, Quantity quantity, qint64 nowMs,
               std::optional<double> value = std::nullopt)
    {
        const qint64 bucket = bucketStartMs(nowMs, m_bucketMs);
        const Key key { sensorKey, quantity };
        const auto it = m_last.constFind(key);
        if (it != m_last.cend() && it->bucket == bucket && it->value == value)
            return false; // same bucket AND unchanged value — a redundant re-broadcast
        m_last.insert(key, { bucket, value });
        return true;
    }

    void reset() { m_last.clear(); }

private:
    struct Key {
        QString sensor;
        Quantity quantity;
        bool operator==(const Key &o) const { return sensor == o.sensor && quantity == o.quantity; }
    };
    struct Last {
        qint64 bucket;
        std::optional<double> value;
    };
    friend qsizetype qHash(const Key &k, qsizetype seed) noexcept
    {
        return qHashMulti(seed, k.sensor, static_cast<int>(k.quantity));
    }

    qint64 m_bucketMs;
    QHash<Key, Last> m_last;
};

} // namespace klr
