// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "catalogentry.h"
#include "csvcatalogrepository.h"

using namespace klr;

// CsvCatalogRepository search + lookup, on a small in-code fixture (no CSV/resource).
class TestCatalogRepository : public QObject {
    Q_OBJECT

private slots:
    void byKeyHitAndMiss();
    void searchEmptyQueryAndLimit();
    void searchIsCaseAndDiacriticInsensitive();
    void searchRanksPrefixBeforeSubstring();
    void searchMatchesCommonName();
};

namespace {

CatalogEntry entry(const QString &key, const QString &common)
{
    CatalogEntry e;
    e.key = key;
    e.commonName = common;
    return e;
}

CsvCatalogRepository fixtureRepo()
{
    return CsvCatalogRepository({
        entry(QStringLiteral("Ficus elastica"), QStringLiteral("Rubber plant")),
        entry(QStringLiteral("Ficus lyrata"), QStringLiteral("Fiddle-leaf fig")),
        entry(QStringLiteral("Aloe vera"), QStringLiteral("Aloe")),
        entry(QString::fromUtf8("Alo\xc3\xab striata"), QStringLiteral("Coral aloe")), // "Aloë striata"
        entry(QStringLiteral("Monstera deliciosa"), QStringLiteral("Swiss cheese plant")),
    });
}

} // namespace

void TestCatalogRepository::byKeyHitAndMiss()
{
    const CsvCatalogRepository repo = fixtureRepo();
    QCOMPARE(repo.count(), 5);

    const std::optional<CatalogEntry> hit = repo.byKey(QStringLiteral("Aloe vera"));
    QVERIFY(hit.has_value());
    QCOMPARE(hit->commonName, QStringLiteral("Aloe"));

    QVERIFY(!repo.byKey(QStringLiteral("Nope nope")).has_value());
    QVERIFY(!repo.byKey(QStringLiteral("aloe vera")).has_value()); // byKey is exact (the stored key)
}

void TestCatalogRepository::searchEmptyQueryAndLimit()
{
    const CsvCatalogRepository repo = fixtureRepo();
    QVERIFY(repo.search(QString(), 10).isEmpty());
    QVERIFY(repo.search(QStringLiteral("   "), 10).isEmpty());
    QVERIFY(repo.search(QStringLiteral("a"), 0).isEmpty());

    const QList<CatalogEntry> limited = repo.search(QStringLiteral("a"), 2);
    QCOMPARE(limited.size(), 2);
}

void TestCatalogRepository::searchIsCaseAndDiacriticInsensitive()
{
    const CsvCatalogRepository repo = fixtureRepo();
    // "aloe" (no diacritic, lower case) must match both "Aloe vera" and "Aloë striata".
    const QList<CatalogEntry> hits = repo.search(QStringLiteral("aloe"), 10);
    QStringList keys;
    for (const CatalogEntry &e : hits)
        keys << e.key;
    QVERIFY(keys.contains(QStringLiteral("Aloe vera")));
    QVERIFY(keys.contains(QString::fromUtf8("Alo\xc3\xab striata")));
}

void TestCatalogRepository::searchRanksPrefixBeforeSubstring()
{
    const CsvCatalogRepository repo = fixtureRepo();
    // "ficus": both entries are botanical-prefix matches, returned alphabetically.
    const QList<CatalogEntry> ficus = repo.search(QStringLiteral("ficus"), 10);
    QCOMPARE(ficus.size(), 2);
    QCOMPARE(ficus.at(0).key, QStringLiteral("Ficus elastica"));
    QCOMPARE(ficus.at(1).key, QStringLiteral("Ficus lyrata"));

    // "plant": only common-name substring matches (Rubber plant, Swiss cheese plant),
    // no botanical hit — they still come back, after any prefix matches would.
    const QList<CatalogEntry> plant = repo.search(QStringLiteral("plant"), 10);
    QStringList keys;
    for (const CatalogEntry &e : plant)
        keys << e.key;
    QVERIFY(keys.contains(QStringLiteral("Ficus elastica")));
    QVERIFY(keys.contains(QStringLiteral("Monstera deliciosa")));
}

void TestCatalogRepository::searchMatchesCommonName()
{
    const CsvCatalogRepository repo = fixtureRepo();
    const QList<CatalogEntry> hits = repo.search(QStringLiteral("fiddle"), 10);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.at(0).key, QStringLiteral("Ficus lyrata"));
}

QTEST_MAIN(TestCatalogRepository)
#include "test_catalogrepository.moc"
