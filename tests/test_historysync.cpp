// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "bucket.h" // kHourMs
#include "historysync.h"

using namespace klr;

// Pure history-sync scheduling math (ADR 0014): how many newest log entries to fetch, and whether a
// sensor is due — both driven by an explicit nowMs, so deterministic.
class TestHistorySync : public QObject {
    Q_OBJECT

    static constexpr qint64 kBase = 1'700'000'000'000; // a fixed "now" in epoch ms

private slots:
    void neverSyncedReadsWholeBuffer()
    {
        QCOMPARE(historyEntriesToRead(384, std::nullopt, kBase), 384);
        QCOMPARE(historyEntriesToRead(0, std::nullopt, kBase), 0); // empty log
    }

    void incrementalReadsHoursSinceLastSyncPlusMargin()
    {
        // Synced 3h ago, 1 entry/hour, default margin 2 -> ~5 newest entries.
        const qint64 lastSync = kBase - 3 * kHourMs;
        QCOMPARE(historyEntriesToRead(384, lastSync, kBase, /*perHour*/ 1, /*margin*/ 2), 5);
    }

    void incrementalCeilsPartialHour()
    {
        // 90 minutes since last sync -> ceil(1.5h) = 2 hours, +2 margin = 4.
        const qint64 lastSync = kBase - (90 * 60 * 1000);
        QCOMPARE(historyEntriesToRead(384, lastSync, kBase, 1, 2), 4);
    }

    void clampedToBufferSize()
    {
        // Synced 1000h ago but the device only holds 384 entries.
        const qint64 lastSync = kBase - 1000 * kHourMs;
        QCOMPARE(historyEntriesToRead(384, lastSync, kBase, 1, 2), 384);
    }

    void justSyncedReadsOnlyMargin()
    {
        QCOMPARE(historyEntriesToRead(384, kBase, kBase, 1, 2), 2);       // elapsed == 0
        QCOMPARE(historyEntriesToRead(384, kBase + 5000, kBase, 1, 2), 2); // clock skew (future)
        QCOMPARE(historyEntriesToRead(1, kBase, kBase, 1, 2), 1);          // margin clamped to count
    }

    void entriesPerHourScales()
    {
        // A 2-entries-per-hour device, 3h since sync -> 3*2 + 2 = 8.
        const qint64 lastSync = kBase - 3 * kHourMs;
        QCOMPARE(historyEntriesToRead(384, lastSync, kBase, /*perHour*/ 2, /*margin*/ 2), 8);
    }

    void dueWhenNeverSyncedOrOlderThanCadence()
    {
        const qint64 cadence = 6 * kHourMs;
        QVERIFY(isHistorySyncDue(std::nullopt, kBase, cadence));            // never synced
        QVERIFY(isHistorySyncDue(kBase - 6 * kHourMs, kBase, cadence));     // exactly one cadence
        QVERIFY(isHistorySyncDue(kBase - 9 * kHourMs, kBase, cadence));     // older
        QVERIFY(!isHistorySyncDue(kBase - 1 * kHourMs, kBase, cadence));    // too recent
        QVERIFY(!isHistorySyncDue(kBase, kBase, cadence));                  // just now
    }
};

QTEST_APPLESS_MAIN(TestHistorySync)
#include "test_historysync.moc"
