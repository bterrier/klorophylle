// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "liveness.h"

using namespace klr;

// The pure connectivity judgment: classify a sensor from its last-broadcast and
// last-value timestamps relative to now. Literal inputs only — no DB, no clock, no BLE.
class TestLiveness : public QObject {
    Q_OBJECT

    static constexpr qint64 kNow = 1'000'000'000'000; // a fixed "now" in ms

private slots:
    void neverObservedIsOffline()
    {
        QCOMPARE(livenessOf(std::nullopt, std::nullopt, kNow), Liveness::Offline);
    }

    void oldBroadcastDominatesIsOffline()
    {
        // Heard 61s ago — offline even though a value arrived 1s ago.
        QCOMPARE(livenessOf(kNow - 61'000, kNow - 1'000, kNow), Liveness::Offline);
    }

    void freshBroadcastNoValueIsStale()
    {
        QCOMPARE(livenessOf(kNow - 5'000, std::nullopt, kNow), Liveness::Stale);
    }

    void freshBroadcastOldValueIsStale()
    {
        // Still broadcasting, but the newest usable value is 61s old.
        QCOMPARE(livenessOf(kNow - 5'000, kNow - 61'000, kNow), Liveness::Stale);
    }

    void freshBroadcastFreshValueIsLive()
    {
        QCOMPARE(livenessOf(kNow - 5'000, kNow - 1'000, kNow), Liveness::Live);
    }

    void offlineBoundaryIsExclusive()
    {
        // Exactly 60s is NOT >60s — still considered heard.
        QCOMPARE(livenessOf(kNow - 60'000, kNow, kNow), Liveness::Live);
        QCOMPARE(livenessOf(kNow - 60'001, kNow, kNow), Liveness::Offline);
    }

    void staleBoundaryIsExclusive()
    {
        // Value exactly at the stale window is fresh; one ms older is stale.
        QCOMPARE(livenessOf(kNow - 1'000, kNow - 60'000, kNow), Liveness::Live);
        QCOMPARE(livenessOf(kNow - 1'000, kNow - 60'001, kNow), Liveness::Stale);
    }

    void futureTimestampReadsAsFresh()
    {
        // Clock skew: a timestamp ahead of now must never flip a sensor offline/stale.
        QCOMPARE(livenessOf(kNow + 1'000, kNow + 1'000, kNow), Liveness::Live);
    }

    void customThresholds()
    {
        // Tighter windows: 10s offline, 10s stale.
        QCOMPARE(livenessOf(kNow - 11'000, kNow, kNow, 10'000, 10'000), Liveness::Offline);
        QCOMPARE(livenessOf(kNow - 5'000, kNow - 11'000, kNow, 10'000, 10'000), Liveness::Stale);
        QCOMPARE(livenessOf(kNow - 5'000, kNow - 5'000, kNow, 10'000, 10'000), Liveness::Live);
    }
};

QTEST_APPLESS_MAIN(TestLiveness)
#include "test_liveness.moc"
