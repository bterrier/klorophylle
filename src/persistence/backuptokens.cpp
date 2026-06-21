// SPDX-License-Identifier: GPL-3.0-or-later
#include "backuptokens.h"

#include <QtCore/QHash>

namespace klr::backuptokens {

QString toToken(Quantity q)
{
    switch (q) {
    case Quantity::SoilMoisture:     return QStringLiteral("SoilMoisture");
    case Quantity::SoilConductivity: return QStringLiteral("SoilConductivity");
    case Quantity::SoilTemperature:  return QStringLiteral("SoilTemperature");
    case Quantity::AirTemperature:   return QStringLiteral("AirTemperature");
    case Quantity::AirHumidity:      return QStringLiteral("AirHumidity");
    case Quantity::Pressure:         return QStringLiteral("Pressure");
    case Quantity::Illuminance:      return QStringLiteral("Illuminance");
    case Quantity::Ppfd:             return QStringLiteral("Ppfd");
    case Quantity::Battery:          return QStringLiteral("Battery");
    case Quantity::Co2:              return QStringLiteral("Co2");
    case Quantity::Voc:              return QStringLiteral("Voc");
    case Quantity::Pm25:             return QStringLiteral("Pm25");
    case Quantity::Pm10:             return QStringLiteral("Pm10");
    case Quantity::Hcho:             return QStringLiteral("Hcho");
    case Quantity::Radioactivity:    return QStringLiteral("Radioactivity");
    case Quantity::WaterTank:        return QStringLiteral("WaterTank");
    case Quantity::Dli:              return QStringLiteral("Dli");
    }
    Q_UNREACHABLE_RETURN({});
}

QString toToken(Unit u)
{
    switch (u) {
    case Unit::None:                 return QStringLiteral("None");
    case Unit::Percent:              return QStringLiteral("Percent");
    case Unit::DegreeCelsius:        return QStringLiteral("DegreeCelsius");
    case Unit::MicroSiemensPerCm:    return QStringLiteral("MicroSiemensPerCm");
    case Unit::Lux:                  return QStringLiteral("Lux");
    case Unit::Micromole:            return QStringLiteral("Micromole");
    case Unit::Pascal:               return QStringLiteral("Pascal");
    case Unit::Hectopascal:          return QStringLiteral("Hectopascal");
    case Unit::Ppm:                  return QStringLiteral("Ppm");
    case Unit::MicrogramPerCubicMetre: return QStringLiteral("MicrogramPerCubicMetre");
    case Unit::MicrosievertPerHour:  return QStringLiteral("MicrosievertPerHour");
    case Unit::DegreeFahrenheit:     return QStringLiteral("DegreeFahrenheit");
    case Unit::InchOfMercury:        return QStringLiteral("InchOfMercury");
    case Unit::MillimetreOfMercury:  return QStringLiteral("MillimetreOfMercury");
    }
    Q_UNREACHABLE_RETURN({});
}

QString toToken(Provenance p)
{
    switch (p) {
    case Provenance::Unknown:       return QStringLiteral("Unknown");
    case Provenance::Live:          return QStringLiteral("Live");
    case Provenance::Advertisement: return QStringLiteral("Advertisement");
    case Provenance::History:       return QStringLiteral("History");
    case Provenance::Probe:         return QStringLiteral("Probe");
    case Provenance::Manual:        return QStringLiteral("Manual");
    }
    Q_UNREACHABLE_RETURN({});
}

QString toToken(JournalEntryKind k)
{
    switch (k) {
    case JournalEntryKind::Note:        return QStringLiteral("Note");
    case JournalEntryKind::Watering:    return QStringLiteral("Watering");
    case JournalEntryKind::Fertilizing: return QStringLiteral("Fertilizing");
    case JournalEntryKind::Repotting:   return QStringLiteral("Repotting");
    case JournalEntryKind::Pruning:     return QStringLiteral("Pruning");
    case JournalEntryKind::Observation: return QStringLiteral("Observation");
    case JournalEntryKind::Memory:      return QStringLiteral("Memory");
    }
    Q_UNREACHABLE_RETURN({});
}

QString toToken(HandleKind h)
{
    switch (h) {
    case HandleKind::Mac:               return QStringLiteral("Mac");
    case HandleKind::CoreBluetoothUuid: return QStringLiteral("CoreBluetoothUuid");
    }
    Q_UNREACHABLE_RETURN({});
}

namespace {

// Build the reverse map once from the forward switch — there is exactly one source of
// truth for each pairing (toToken), so the inverse can never silently disagree with it.
template <class E>
std::optional<E> reverse(const QString &token, int count)
{
    for (int i = 0; i < count; ++i) {
        const auto v = static_cast<E>(i);
        if (toToken(v) == token)
            return v;
    }
    return std::nullopt;
}

} // namespace

template <>
std::optional<Quantity> fromToken<Quantity>(const QString &token)
{
    return reverse<Quantity>(token, kQuantityCount);
}

template <>
std::optional<Unit> fromToken<Unit>(const QString &token)
{
    return reverse<Unit>(token, static_cast<int>(Unit::MillimetreOfMercury) + 1);
}

template <>
std::optional<Provenance> fromToken<Provenance>(const QString &token)
{
    return reverse<Provenance>(token, static_cast<int>(Provenance::Manual) + 1);
}

template <>
std::optional<JournalEntryKind> fromToken<JournalEntryKind>(const QString &token)
{
    return reverse<JournalEntryKind>(token, static_cast<int>(JournalEntryKind::Memory) + 1);
}

template <>
std::optional<HandleKind> fromToken<HandleKind>(const QString &token)
{
    return reverse<HandleKind>(token, static_cast<int>(HandleKind::CoreBluetoothUuid) + 1);
}

} // namespace klr::backuptokens
