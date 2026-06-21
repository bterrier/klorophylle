// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "contextbuilder.h"
#include "inmemoryplantrepository.h"
#include "plant.h"

using namespace klr;

// The deterministic, roster-only, CLOCK-FREE context block (docs/adr/0019 +
// cache-placement follow-up). Pure over an in-memory plant repository, so the whole
// block is asserted as literal text and proven byte-stable across calls (the context-block
// exit criterion). trackedSince still drives the oldest-first sort but never reaches the text.
namespace {

// An arbitrary epoch anchor: trackedSince only affects sort order now, not the output.
constexpr qint64 kAnchorMs = 1737331200000LL;

Plant plantTracked(const QString &name, const QString &species, qint64 trackedMs)
{
    return Plant{PlantId::generate(), name, species,
                 QDateTime::fromMSecsSinceEpoch(trackedMs, QTimeZone::UTC)};
}

} // namespace

class TestContextBuilder : public QObject
{
    Q_OBJECT

private slots:
    void emptyRoster()
    {
        InMemoryPlantRepository plants;
        ContextBuilder builder(plants);
        QCOMPARE(builder.build(), QStringLiteral("No plants are being tracked yet."));
    }

    void singlePlantDaysAndSpecies()
    {
        InMemoryPlantRepository plants;
        plants.add(plantTracked(QStringLiteral("Basil"), QStringLiteral("Ocimum basilicum"),
                                kAnchorMs - 12LL * 24 * 60 * 60 * 1000));
        ContextBuilder builder(plants);

        const QString expected = QStringLiteral(
            "Plant roster (1 plant tracked):\n"
            "- Basil (species Ocimum basilicum)\n");
        QCOMPARE(builder.build(), expected);
    }

    void manyPlantsSortedOldestFirst()
    {
        InMemoryPlantRepository plants;
        // Insert out of chronological order; build() must sort oldest-first.
        plants.add(plantTracked(QStringLiteral("Fern"), QString(),
                                kAnchorMs - 1LL * 24 * 60 * 60 * 1000)); // newer, no species
        plants.add(plantTracked(QStringLiteral("Basil"), QStringLiteral("Ocimum basilicum"),
                                kAnchorMs - 30LL * 24 * 60 * 60 * 1000)); // older
        ContextBuilder builder(plants);

        const QString expected = QStringLiteral(
            "Plant roster (2 plants tracked):\n"
            "- Basil (species Ocimum basilicum)\n"
            "- Fern (species unknown)\n");
        QCOMPARE(builder.build(), expected);
    }

    void unnamedAndZeroDays()
    {
        InMemoryPlantRepository plants;
        plants.add(plantTracked(QString(), QString(), kAnchorMs));
        ContextBuilder builder(plants);

        const QString expected = QStringLiteral(
            "Plant roster (1 plant tracked):\n"
            "- (unnamed) (species unknown)\n");
        QCOMPARE(builder.build(), expected);
    }

    void deterministicAcrossCalls()
    {
        InMemoryPlantRepository plants;
        plants.add(plantTracked(QStringLiteral("Basil"), QStringLiteral("Ocimum basilicum"),
                                kAnchorMs - 12LL * 24 * 60 * 60 * 1000));
        plants.add(plantTracked(QStringLiteral("Fern"), QStringLiteral("Nephrolepis"),
                                kAnchorMs - 5LL * 24 * 60 * 60 * 1000));
        ContextBuilder builder(plants);

        // Byte-identical across repeated builds — the exit criterion (no clock to drift).
        QCOMPARE(builder.build(), builder.build());
    }
};

QTEST_GUILESS_MAIN(TestContextBuilder)
#include "test_contextbuilder.moc"
