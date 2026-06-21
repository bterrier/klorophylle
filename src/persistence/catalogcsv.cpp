// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogcsv.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace klr {

namespace {

// Blank cell -> nullopt; otherwise the parsed double, or nullopt if it isn't numeric
// (the size columns carry "≥10" etc., but those aren't among the fields we read).
std::optional<double> num(const QString &cell)
{
    const QString s = cell.trimmed();
    if (s.isEmpty())
        return std::nullopt;
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

// 0-based column indices into the legacy CSV (35 columns, see the header row).
enum Col {
    PlantName = 0,
    CommonName = 1,
    SoilMoistureMin = 21,
    SoilMoistureMax = 22,
    SoilConductivityMin = 23,
    SoilConductivityMax = 24,
    SoilPhMin = 25,
    SoilPhMax = 26,
    TemperatureMin = 27,
    TemperatureMax = 28,
    HumidityMin = 29,
    HumidityMax = 30,
    LightLuxMin = 31,
    LightLuxMax = 32,
    LightMmolMin = 33,
    LightMmolMax = 34,
};

} // namespace

QList<CatalogEntry> CatalogCsv::parse(QByteArrayView csv)
{
    const QString text = QString::fromUtf8(csv);
    // Split on either line ending; keep empty parts out.
    const QList<QStringView> lines = QStringView(text).split(u'\n', Qt::SkipEmptyParts);

    QList<CatalogEntry> out;
    out.reserve(lines.size());

    bool headerSkipped = false;
    for (QStringView rawLine : lines) {
        const QString line = rawLine.trimmed().toString(); // drops a trailing '\r'
        if (line.isEmpty())
            continue;
        if (!headerSkipped) { // the first non-empty line is the column header
            headerSkipped = true;
            continue;
        }

        const QStringList f = line.split(u';');
        const auto cell = [&f](int i) -> QString { return f.value(i); };

        const QString key = cell(PlantName).trimmed();
        if (key.isEmpty()) // a row without a botanical name carries no usable identity
            continue;

        CatalogEntry e;
        e.key = key;
        e.commonName = cell(CommonName).trimmed();
        e.soilMoistureMin = num(cell(SoilMoistureMin));
        e.soilMoistureMax = num(cell(SoilMoistureMax));
        e.soilConductivityMin = num(cell(SoilConductivityMin));
        e.soilConductivityMax = num(cell(SoilConductivityMax));
        e.soilPhMin = num(cell(SoilPhMin));
        e.soilPhMax = num(cell(SoilPhMax));
        e.temperatureMin = num(cell(TemperatureMin));
        e.temperatureMax = num(cell(TemperatureMax));
        e.humidityMin = num(cell(HumidityMin));
        e.humidityMax = num(cell(HumidityMax));
        e.lightLuxMin = num(cell(LightLuxMin));
        e.lightLuxMax = num(cell(LightLuxMax));
        e.lightMmolMin = num(cell(LightMmolMin));
        e.lightMmolMax = num(cell(LightMmolMax));
        out.append(std::move(e));
    }

    return out;
}

} // namespace klr
