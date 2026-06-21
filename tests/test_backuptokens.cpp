// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "backuptokens.h"
#include "journalentry.h"
#include "reading.h"
#include "sensor.h"

#include <QtCore/QSet>

using namespace klr;
using namespace klr::backuptokens;

// Total-coverage guard for the backup enum<->token table (ADR 0010). For each of the
// five enums we walk EVERY enumerator (by integer range, tied to the last value) and
// assert: a non-empty token, a clean round-trip, and table-wide token uniqueness. Adding
// an enumerator without a token therefore fails here (and fails to compile in
// backuptokens.cpp, which has no default case). Tokens are the wire format, so a rename
// is a deliberate format change, not a refactor.
class TestBackupTokens : public QObject {
    Q_OBJECT

    // Round-trip + non-empty + uniqueness over [0, count) of enum E.
    template <class E>
    void checkEnum(int count)
    {
        QSet<QString> seen;
        for (int i = 0; i < count; ++i) {
            const auto value = static_cast<E>(i);
            const QString token = toToken(value);
            QVERIFY2(!token.isEmpty(), qPrintable(QStringLiteral("empty token for value %1").arg(i)));
            QVERIFY2(!seen.contains(token), qPrintable(QStringLiteral("duplicate token %1").arg(token)));
            seen.insert(token);

            const std::optional<E> back = fromToken<E>(token);
            QVERIFY2(back.has_value(), qPrintable(QStringLiteral("token %1 did not reverse").arg(token)));
            QVERIFY(back.value() == value);
        }
    }

private slots:
    void quantityTotalCoverage() { checkEnum<Quantity>(kQuantityCount); }
    void unitTotalCoverage() { checkEnum<Unit>(static_cast<int>(Unit::MillimetreOfMercury) + 1); }
    void provenanceTotalCoverage() { checkEnum<Provenance>(static_cast<int>(Provenance::Manual) + 1); }
    void journalKindTotalCoverage()
    {
        checkEnum<JournalEntryKind>(static_cast<int>(JournalEntryKind::Memory) + 1);
    }
    void handleKindTotalCoverage()
    {
        checkEnum<HandleKind>(static_cast<int>(HandleKind::CoreBluetoothUuid) + 1);
    }

    void tokensAreTheEnumeratorNames()
    {
        // Pin a few so a token rename is caught here as the breaking change it is.
        QCOMPARE(toToken(Quantity::SoilMoisture), QStringLiteral("SoilMoisture"));
        QCOMPARE(toToken(Quantity::Dli), QStringLiteral("Dli")); // derived quantity, still backed up
        QCOMPARE(toToken(Unit::DegreeCelsius), QStringLiteral("DegreeCelsius"));
        QCOMPARE(toToken(Provenance::History), QStringLiteral("History"));
        QCOMPARE(toToken(JournalEntryKind::Watering), QStringLiteral("Watering"));
        QCOMPARE(toToken(JournalEntryKind::Memory), QStringLiteral("Memory")); // agent memory
        QCOMPARE(toToken(HandleKind::Mac), QStringLiteral("Mac"));
    }

    void unknownTokenIsNullopt()
    {
        QVERIFY(!fromToken<Quantity>(QStringLiteral("Nonexistent")).has_value());
        QVERIFY(!fromToken<Unit>(QString()).has_value());
        QVERIFY(!fromToken<Provenance>(QStringLiteral("live")).has_value()); // case-sensitive
        QVERIFY(!fromToken<JournalEntryKind>(QStringLiteral("Photo")).has_value());
        QVERIFY(!fromToken<HandleKind>(QStringLiteral("Ble")).has_value());
    }
};

QTEST_GUILESS_MAIN(TestBackupTokens)
#include "test_backuptokens.moc"
