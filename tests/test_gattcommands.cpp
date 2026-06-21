// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "device_bparasite.h"
#include "device_flowercare.h"
#include "device_hygrotemp_clock.h"
#include "device_parrotpot.h"
#include "device_ropot.h"
#include "device_thermobeacon.h"
#include "device_wp6003.h"

#include <optional>

using namespace klr;

// The per-device write-command builders (ADR 0026) are pure: a DeviceAction maps to a fixed
// service/characteristic/payload (or, for ClockSync, a payload built from an injected `now`). They
// are the only write capability ported from WatchFlower, so the bytes matter — assert them exactly.
// No Bluetooth stack here, the same pure seam as the advertisement / GATT-read decoders.
class TestGattCommands : public QObject {
    Q_OBJECT

    // The command for an action, or nullopt if the device doesn't expose it.
    static std::optional<GattCommand> cmd(const QList<GattCommand> &cmds, DeviceAction a)
    {
        for (const GattCommand &c : cmds)
            if (c.action == a)
                return c;
        return std::nullopt;
    }
    static QByteArray hex(const char *h) { return QByteArray::fromHex(h); }

private slots:
    void flowerCare()
    {
        const auto cmds = DeviceFlowerCare{}.gattCommands(QDateTime());
        QCOMPARE(int(cmds.size()), 2);

        const auto led = cmd(cmds, DeviceAction::LedBlink);
        QVERIFY(led.has_value());
        QCOMPARE(led->service, QStringLiteral("00001204-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(led->characteristic, QStringLiteral("00001a00-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(led->payload, hex("fdff"));
        QVERIFY(led->writeWithoutResponse);
        QVERIFY(!led->handshake.has_value());

        const auto clear = cmd(cmds, DeviceAction::ClearData);
        QVERIFY(clear.has_value());
        QCOMPARE(clear->service, QStringLiteral("00001206-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(clear->characteristic, QStringLiteral("00001a10-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(clear->payload, hex("a20000"));
        QVERIFY(!clear->writeWithoutResponse);
        // Clear-history sits behind the MiBeacon RC4 handshake (product id 0x0098).
        QVERIFY(clear->handshake.has_value());
        QCOMPARE(clear->handshake->productId, quint16(0x0098));
        QCOMPARE(clear->handshake->service, QStringLiteral("0000fe95-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(clear->handshake->startCharacteristic, QStringLiteral("00000010-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(clear->handshake->authCharacteristic, QStringLiteral("00000001-0000-1000-8000-00805f9b34fb"));
    }

    void roPot()
    {
        // RoPot has no working LED (a TODO stub in WatchFlower) — clear-history only, productId 0x015D.
        const auto cmds = DeviceRoPot{}.gattCommands(QDateTime());
        QCOMPARE(int(cmds.size()), 1);
        QVERIFY(!cmd(cmds, DeviceAction::LedBlink).has_value());

        const auto clear = cmd(cmds, DeviceAction::ClearData);
        QVERIFY(clear.has_value());
        QCOMPARE(clear->payload, hex("a20000"));
        QVERIFY(clear->handshake.has_value());
        QCOMPARE(clear->handshake->productId, quint16(0x015D));
    }

    void parrotPot()
    {
        const auto cmds = DeviceParrotPot{}.gattCommands(QDateTime());
        QCOMPARE(int(cmds.size()), 2);

        const auto led = cmd(cmds, DeviceAction::LedBlink);
        QVERIFY(led.has_value());
        QCOMPARE(led->service, QStringLiteral("39e1fa00-84a8-11e2-afba-0002a5d5c51b"));
        QCOMPARE(led->characteristic, QStringLiteral("39e1fa07-84a8-11e2-afba-0002a5d5c51b"));
        QCOMPARE(led->payload, hex("01"));
        QVERIFY(!led->writeWithoutResponse);

        const auto water = cmd(cmds, DeviceAction::Watering);
        QVERIFY(water.has_value());
        QCOMPARE(water->service, QStringLiteral("39e1f900-84a8-11e2-afba-0002a5d5c51b"));
        QCOMPARE(water->characteristic, QStringLiteral("39e1f906-84a8-11e2-afba-0002a5d5c51b"));
        QCOMPARE(water->payload, hex("0800"));
    }

    void thermoBeacon()
    {
        const auto cmds = DeviceThermoBeacon{}.gattCommands(QDateTime());
        QCOMPARE(int(cmds.size()), 2);
        const QString tx = QStringLiteral("0000fff5-0000-1000-8000-00805f9b34fb");

        const auto led = cmd(cmds, DeviceAction::LedBlink);
        QVERIFY(led.has_value());
        QCOMPARE(led->characteristic, tx);
        QCOMPARE(led->payload, hex("0400000000"));

        const auto clear = cmd(cmds, DeviceAction::ClearData);
        QVERIFY(clear.has_value());
        QCOMPARE(clear->characteristic, tx);
        QCOMPARE(clear->payload, hex("0200000000"));
    }

    void hygrotempClockSync()
    {
        // epoch 0x01020304 (LE -> 04 03 02 01) at UTC+1 (offset byte 0x01).
        const QDateTime now = QDateTime::fromSecsSinceEpoch(0x01020304, QTimeZone::fromSecondsAheadOfUtc(3600));
        QCOMPARE(DeviceHygrotempClock::clockSyncPayload(now), hex("0403020101"));

        const auto cmds = DeviceHygrotempClock{}.gattCommands(now);
        QCOMPARE(int(cmds.size()), 1);
        const auto sync = cmd(cmds, DeviceAction::ClockSync);
        QVERIFY(sync.has_value());
        QCOMPARE(sync->service, QStringLiteral("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6"));
        QCOMPARE(sync->characteristic, QStringLiteral("ebe0ccb7-7a0a-4b0c-8a1a-6ff2997da3a6"));
        QCOMPARE(sync->payload, hex("0403020101"));
    }

    void wp6003()
    {
        // calibrate is a single byte; clock-sync is "aa" + yy mm dd HH MM SS (2-digit year).
        QCOMPARE(DeviceWP6003::calibratePayload(), hex("ad"));
        const QDateTime now(QDate(2024, 1, 15), QTime(10, 30, 45));
        QCOMPARE(DeviceWP6003::clockSyncPayload(now), hex("aa18010f0a1e2d"));

        const auto cmds = DeviceWP6003{}.gattCommands(now);
        QCOMPARE(int(cmds.size()), 2);
        const QString tx = QStringLiteral("0000fff1-0000-1000-8000-00805f9b34fb");

        const auto cal = cmd(cmds, DeviceAction::Calibrate);
        QVERIFY(cal.has_value());
        QCOMPARE(cal->characteristic, tx);
        QCOMPARE(cal->payload, hex("ad"));
        QVERIFY(cal->writeWithoutResponse);

        const auto sync = cmd(cmds, DeviceAction::ClockSync);
        QVERIFY(sync.has_value());
        QCOMPARE(sync->payload, hex("aa18010f0a1e2d"));
    }

    void noCommandsByDefault()
    {
        // A device that doesn't override gattCommands() (advertisement-only b-parasite) inherits
        // the base default: an empty command set.
        QVERIFY(DeviceBParasite{}.gattCommands(QDateTime()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestGattCommands)
#include "test_gattcommands.moc"
