// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "catalogcsv.h"

#include <initializer_list>
#include <utility>

using namespace klr;

// The catalog CSV parser is a pure function — exercised here against inline fixtures
// (no bundled-resource dependency, which keeps the test independent of static-lib
// resource init). Rows are built by COLUMN INDEX so the test never depends on
// hand-counting 35 semicolons.
class TestCatalogCsv : public QObject {
    Q_OBJECT

private slots:
    void skipsHeaderAndEmptyRows();
    void mapsNamesAndRanges();
    void blankNumericCellsBecomeNullopt();
    void skipsRowsWithoutBotanicalName();
    void toleratesDecimalsCrlfAndSizeColumns();
};

namespace {

// 0-based column indices (match catalogcsv.cpp).
constexpr int kCols = 35;
enum { PlantName = 0, CommonName = 1, Classification = 3, Diameter = 4,
       SoilMoistureMin = 21, SoilMoistureMax = 22, SoilConductivityMin = 23,
       SoilPhMin = 25, SoilPhMax = 26, TemperatureMin = 27, TemperatureMax = 28,
       LightMmolMax = 34 };

// One CSV line with the given cells placed by index; production has a trailing ';'.
QString row(std::initializer_list<std::pair<int, QString>> cells)
{
    QStringList f;
    for (int i = 0; i < kCols; ++i)
        f << QString();
    for (const auto &c : cells)
        f[c.first] = c.second;
    return f.join(u';') + QStringLiteral(";");
}

QByteArray fixture(const QStringList &bodyRows)
{
    QStringList lines;
    lines << row({ { PlantName, QStringLiteral("HEADER") } }); // first line is skipped
    lines << bodyRows;
    return lines.join(u'\n').toUtf8();
}

} // namespace

void TestCatalogCsv::skipsHeaderAndEmptyRows()
{
    const QByteArray csv = fixture({
        row({ { PlantName, QStringLiteral("Ficus elastica") } }),
        QString(), // blank line
        row({ { PlantName, QStringLiteral("Aloe vera") } }),
    });
    const QList<CatalogEntry> out = CatalogCsv::parse(csv);
    QCOMPARE(out.size(), 2);
    QCOMPARE(out.at(0).key, QStringLiteral("Ficus elastica"));
    QCOMPARE(out.at(1).key, QStringLiteral("Aloe vera"));
}

void TestCatalogCsv::mapsNamesAndRanges()
{
    const QByteArray csv = fixture({ row({
        { PlantName, QStringLiteral("Ficus elastica") },
        { CommonName, QStringLiteral("Rubber plant") },
        { SoilMoistureMin, QStringLiteral("15") },
        { SoilMoistureMax, QStringLiteral("60") },
        { SoilConductivityMin, QStringLiteral("350") },
        { TemperatureMin, QStringLiteral("15") },
        { TemperatureMax, QStringLiteral("30") },
        { LightMmolMax, QStringLiteral("15") },
    }) });
    const QList<CatalogEntry> out = CatalogCsv::parse(csv);
    QCOMPARE(out.size(), 1);
    const CatalogEntry &e = out.at(0);
    QCOMPARE(e.key, QStringLiteral("Ficus elastica"));
    QCOMPARE(e.commonName, QStringLiteral("Rubber plant"));
    QCOMPARE(e.soilMoistureMin, std::optional<double>(15));
    QCOMPARE(e.soilMoistureMax, std::optional<double>(60));
    QCOMPARE(e.soilConductivityMin, std::optional<double>(350));
    QCOMPARE(e.temperatureMin, std::optional<double>(15));
    QCOMPARE(e.temperatureMax, std::optional<double>(30));
    QCOMPARE(e.lightMmolMax, std::optional<double>(15));
}

void TestCatalogCsv::blankNumericCellsBecomeNullopt()
{
    const QByteArray csv = fixture({ row({ { PlantName, QStringLiteral("Mystery sp.") } }) });
    const QList<CatalogEntry> out = CatalogCsv::parse(csv);
    QCOMPARE(out.size(), 1);
    const CatalogEntry &e = out.at(0);
    QCOMPARE(e.key, QStringLiteral("Mystery sp."));
    QVERIFY(e.commonName.isEmpty());
    QVERIFY(!e.soilMoistureMin.has_value());
    QVERIFY(!e.temperatureMax.has_value());
    QVERIFY(!e.lightLuxMin.has_value());
}

void TestCatalogCsv::skipsRowsWithoutBotanicalName()
{
    const QByteArray csv = fixture({
        row({ { CommonName, QStringLiteral("Orphan common name") } }), // no botanical name
        row({ { PlantName, QStringLiteral("Real plant") } }),
    });
    const QList<CatalogEntry> out = CatalogCsv::parse(csv);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0).key, QStringLiteral("Real plant"));
}

void TestCatalogCsv::toleratesDecimalsCrlfAndSizeColumns()
{
    // Decimal pH parses; the ≥-prefixed size column and the comma-list classification
    // don't break the row (we don't read them). A trailing '\r' (CRLF) is trimmed.
    QString r = row({
        { PlantName, QStringLiteral("Abelia chinensis") },
        { Classification, QStringLiteral("Caprifoliaceae,Abelia") },
        { Diameter, QString::fromUtf8("\xe2\x89\xa5" "10") }, // "≥10"
        { SoilMoistureMin, QStringLiteral("15") },
        { SoilPhMin, QStringLiteral("5.5") },
        { SoilPhMax, QStringLiteral("6.5") },
    });
    r += QStringLiteral("\r"); // CRLF line ending (join adds '\n' after the header)
    const QByteArray csv = fixture({ r });
    const QList<CatalogEntry> out = CatalogCsv::parse(csv);
    QCOMPARE(out.size(), 1);
    const CatalogEntry &e = out.at(0);
    QCOMPARE(e.key, QStringLiteral("Abelia chinensis"));
    QCOMPARE(e.soilPhMin, std::optional<double>(5.5));
    QCOMPARE(e.soilPhMax, std::optional<double>(6.5));
    QCOMPARE(e.soilMoistureMin, std::optional<double>(15));
}

QTEST_MAIN(TestCatalogCsv)
#include "test_catalogcsv.moc"
