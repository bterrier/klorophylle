// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "bucket.h"
#include "reading.h"

#include <set>

using namespace klr;

// The bucketing key + write-cadence gate: one clock drives both, so a
// bucket can never be skipped. See ../docs/adr/0006-history-charts-import.md.
class TestBucket : public QObject {
    Q_OBJECT

private slots:
    void floorsToBoundary()
    {
        // Hourly: any sub-hour time shares the bucket; the exact boundary is its own.
        QCOMPARE(bucketStartMs(0, kHourMs), qint64(0));
        QCOMPARE(bucketStartMs(kHourMs - 1, kHourMs), qint64(0));
        QCOMPARE(bucketStartMs(kHourMs, kHourMs), kHourMs);
        QCOMPARE(bucketStartMs(kHourMs + 59 * 60'000, kHourMs), kHourMs);
        QCOMPARE(bucketStartMs(2 * kHourMs + 1, kHourMs), 2 * kHourMs);
        // bucketMs <= 0 disables bucketing (timestamp is its own bucket).
        QCOMPARE(bucketStartMs(12345, 0), qint64(12345));
    }

    void gateAdmitsOncePerBucket()
    {
        WriteCadenceGate gate(kHourMs);
        // First sample of an hour is admitted; the rest of that hour are suppressed.
        QVERIFY(gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, 0));
        QVERIFY(!gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, 60'000));
        QVERIFY(!gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, kHourMs - 1));
        // New hour → admitted again.
        QVERIFY(gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, kHourMs));
    }

    void gateReadmitsOnValueChange()
    {
        WriteCadenceGate gate(kHourMs);
        // First value of the hour is written; an identical repeat is suppressed...
        QVERIFY(gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, 0, 40.0));
        QVERIFY(!gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, 60'000, 40.0));
        // ...but a CHANGED value in the same hour is written (keeps the live display
        // fresh; the repo upserts it into the same bucket row).
        QVERIFY(gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, 120'000, 38.0));
        QVERIFY(!gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, 180'000, 38.0));
    }

    void gateIsPerSeries()
    {
        WriteCadenceGate gate(kHourMs);
        QVERIFY(gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, 0));
        // Same instant, different quantity or sensor → independent series, all admitted.
        QVERIFY(gate.admit(QStringLiteral("s"), Quantity::AirTemperature, 0));
        QVERIFY(gate.admit(QStringLiteral("other"), Quantity::SoilMoisture, 0));
    }

    // The misalignment WatchFlower could hit: a write cadence misaligned with the
    // bucket boundary skips a bucket. Drive a multi-day stream and assert the gate
    // admits EXACTLY ONCE per occupied bucket — no skip, no duplicate.
    void multiDayNoSkipNoDup()
    {
        WriteCadenceGate gate(kHourMs);
        // A 25-minute cadence is the worst case: it never lines up with the hour, so a
        // naive rolling-60-min gate would drift and drop a clock-hour now and then.
        const qint64 stepMs = 25 * 60'000;
        const qint64 horizon = 5LL * 24 * kHourMs; // 5 days

        std::set<qint64> occupied; // every bucket that received >=1 sample
        int admitted = 0;
        std::set<qint64> admittedBuckets;
        for (qint64 t = 0; t <= horizon; t += stepMs) {
            occupied.insert(bucketStartMs(t, kHourMs));
            if (gate.admit(QStringLiteral("s"), Quantity::SoilMoisture, t)) {
                ++admitted;
                admittedBuckets.insert(bucketStartMs(t, kHourMs));
            }
        }
        // One admit per occupied bucket: no bucket skipped, none written twice.
        QCOMPARE(size_t(admitted), occupied.size());
        QCOMPARE(admittedBuckets, occupied);
    }
};

QTEST_GUILESS_MAIN(TestBucket)
#include "test_bucket.moc"
