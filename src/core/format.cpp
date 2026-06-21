// SPDX-License-Identifier: GPL-3.0-or-later
#include "format.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QLocale>

namespace klr {

QString formatAgo(const QDateTime &t, qint64 nowMs)
{
    if (!t.isValid())
        return {};
    const qint64 secs = (nowMs - t.toMSecsSinceEpoch()) / 1000;
    if (secs < 5)
        return QCoreApplication::translate("Format", "just now");
    if (secs < 60)
        return QCoreApplication::translate("Format", "%1s ago").arg(secs);
    if (secs < 3600)
        return QCoreApplication::translate("Format", "%1m ago").arg(secs / 60);
    if (secs < 86400)
        return QCoreApplication::translate("Format", "%1h ago").arg(secs / 3600);
    return QLocale().toString(t.toLocalTime(), QLocale::ShortFormat);
}

QString label(Quantity q)
{
    switch (q) {
    case Quantity::SoilMoisture:     return QStringLiteral("Soil moisture");
    case Quantity::SoilConductivity: return QStringLiteral("Soil fertility");
    case Quantity::SoilTemperature:  return QStringLiteral("Soil temperature");
    case Quantity::AirTemperature:   return QStringLiteral("Temperature");
    case Quantity::AirHumidity:      return QStringLiteral("Humidity");
    case Quantity::Pressure:         return QStringLiteral("Pressure");
    case Quantity::Illuminance:      return QStringLiteral("Light");
    case Quantity::Ppfd:             return QStringLiteral("PPFD");
    case Quantity::Battery:          return QStringLiteral("Battery");
    case Quantity::Co2:              return QStringLiteral("CO₂");
    case Quantity::Voc:              return QStringLiteral("VOC");
    case Quantity::Pm25:             return QStringLiteral("PM2.5");
    case Quantity::Pm10:             return QStringLiteral("PM10");
    case Quantity::Hcho:             return QStringLiteral("Formaldehyde");
    case Quantity::Radioactivity:    return QStringLiteral("Radioactivity");
    case Quantity::WaterTank:        return QStringLiteral("Water tank");
    case Quantity::Dli:              return QStringLiteral("Daily light");
    }
    return QString();
}

QString unitSymbol(Unit u)
{
    switch (u) {
    case Unit::None:                   return QString();
    case Unit::Percent:                return QStringLiteral("%");
    case Unit::DegreeCelsius:          return QStringLiteral("°C");
    case Unit::MicroSiemensPerCm:      return QStringLiteral("µS/cm");
    case Unit::Lux:                    return QStringLiteral("lux");
    case Unit::Micromole:              return QStringLiteral("µmol");
    case Unit::Pascal:                 return QStringLiteral("Pa");
    case Unit::Hectopascal:            return QStringLiteral("hPa");
    case Unit::Ppm:                    return QStringLiteral("ppm");
    case Unit::MicrogramPerCubicMetre: return QStringLiteral("µg/m³");
    case Unit::MicrosievertPerHour:    return QStringLiteral("µSv/h");
    case Unit::DegreeFahrenheit:       return QStringLiteral("°F");
    case Unit::InchOfMercury:          return QStringLiteral("inHg");
    case Unit::MillimetreOfMercury:    return QStringLiteral("mmHg");
    }
    return QString();
}

QString dliUnitSymbol()
{
    return QStringLiteral("mmol/m²/day");
}

namespace {

// Decimals to show for a value displayed in `unit` — whole numbers for coarse,
// large-range quantities; one decimal otherwise (inHg needs two to be meaningful).
int decimalsFor(Unit unit)
{
    switch (unit) {
    case Unit::Lux:
    case Unit::Micromole:
    case Unit::MicroSiemensPerCm:
    case Unit::Ppm:
    case Unit::MicrogramPerCubicMetre:
        return 0;
    case Unit::InchOfMercury:
        return 2;
    default:
        return 1;
    }
}

QString formatNumberWithUnit(double value, Unit unit)
{
    const QString num = QString::number(value, 'f', decimalsFor(unit));
    const QString sym = unitSymbol(unit);
    return sym.isEmpty() ? num : (num + QChar(u' ') + sym);
}

} // namespace

namespace {

// The Daily Light Integral has no Unit enumerator (it is a derived dose) — format it with
// its own symbol and whole-number precision (doses run into the thousands of mmol).
QString formatDli(double dose)
{
    return QString::number(dose, 'f', 0) + QChar(u' ') + dliUnitSymbol();
}

} // namespace

QString formatValue(const Reading &r)
{
    if (!r.value)
        return QStringLiteral("—"); // em dash: absent (std::nullopt, not -99)
    if (r.quantity == Quantity::Dli)
        return formatDli(*r.value);
    return formatNumberWithUnit(*r.value, r.unit);
}

QString formatValue(const Reading &r, const DisplayUnits &units)
{
    if (!r.value)
        return QStringLiteral("—"); // em dash: absent (std::nullopt, not -99)
    if (r.quantity == Quantity::Dli)
        return formatDli(*r.value); // no display-unit alternate — always mmol·m⁻²·day⁻¹

    const Unit disp = displayUnit(r.quantity, units);
    return formatNumberWithUnit(convert(*r.value, r.unit, disp), disp);
}

} // namespace klr
