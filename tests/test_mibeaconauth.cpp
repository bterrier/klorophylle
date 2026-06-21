// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "mibeacon_auth.h"

using namespace klr;

// The vendored ARC4 + the Flower Care-family MiBeacon handshake (ADR 0014). The vectors are
// authoritative: the RC4 KAT is the canonical Wikipedia/RFC example, and the handshake vectors were
// cross-computed with an independent Python ARC4 over the documented seeds.
class TestMiBeaconAuth : public QObject {
    Q_OBJECT

    static QByteArray mac() // C4:7C:8D:6A:00:01
    {
        return QByteArray::fromHex("C47C8D6A0001");
    }

private slots:
    void rc4KnownAnswer()
    {
        // Standard RC4 test vector.
        QCOMPARE(rc4(QByteArrayLiteral("Key"), QByteArrayLiteral("Plaintext")).toHex(),
                 QByteArray("bbf316e8d940af0ad3"));
        // Symmetric: encrypt then decrypt round-trips.
        const QByteArray key = QByteArrayLiteral("a secret");
        const QByteArray msg = QByteArrayLiteral("the quick brown fox");
        QCOMPARE(rc4(key, rc4(key, msg)), msg);
        // Empty key passes data through unchanged.
        QCOMPARE(rc4(QByteArray(), msg), msg);
    }

    void flowerCareHandshakeVectors()
    {
        const MiBeaconHandshake h = mibeaconHandshake(mac(), 0x0098); // Flower Care
        QCOMPARE(h.challenge.size(), qsizetype(12));
        QCOMPARE(h.finish.size(), qsizetype(4));
        QCOMPARE(h.challenge.toHex(), QByteArray("f3f88c4a51df27395457389f"));
        QCOMPARE(h.finish.toHex(), QByteArray("00dafc33"));
    }

    void ropotDiffersByProductId()
    {
        const MiBeaconHandshake h = mibeaconHandshake(mac(), 0x015D); // RoPot
        QCOMPARE(h.challenge.toHex(), QByteArray("594d27fe66a7d550f1554aac"));
        // The finish token is independent of MAC/PID (rc4 of the fixed seeds).
        QCOMPARE(h.finish.toHex(), QByteArray("00dafc33"));
        QVERIFY(h.challenge != mibeaconHandshake(mac(), 0x0098).challenge);
    }

    void shortMacYieldsEmpty()
    {
        const MiBeaconHandshake h = mibeaconHandshake(QByteArray::fromHex("C47C8D"), 0x0098);
        QVERIFY(h.challenge.isEmpty());
        QVERIFY(h.finish.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestMiBeaconAuth)
#include "test_mibeaconauth.moc"
