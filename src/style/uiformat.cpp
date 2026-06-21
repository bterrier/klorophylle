// SPDX-License-Identifier: GPL-3.0-or-later
#include "uiformat.h"

#include "carestatus.h" // klr_core: CareStatus / CareLevel
#include "format.h"     // klr_core: free functions klr::label() / klr::unitSymbol()
#include "reading.h"    // klr_core: Quantity, Unit, kQuantityCount

namespace klr {

QString Format::quantityLabel(int quantity) const
{
    if (quantity < 0 || quantity >= kQuantityCount)
        return {};
    return label(static_cast<Quantity>(quantity));
}

QString Format::unitSymbol(int unit) const
{
    if (unit < static_cast<int>(Unit::None) || unit > static_cast<int>(Unit::MicrosievertPerHour))
        return {};
    return klr::unitSymbol(static_cast<Unit>(unit));
}

QString Format::careStatusLabel(int status) const
{
    switch (static_cast<CareStatus>(status)) {
    case CareStatus::TooLow:  return tr("Too low");
    case CareStatus::Ideal:   return tr("Ideal");
    case CareStatus::TooHigh: return tr("Too high");
    case CareStatus::Unknown: break;
    }
    return {};
}

QString Format::careLevelLabel(int level) const
{
    switch (static_cast<CareLevel>(level)) {
    case CareLevel::Good:      return tr("Healthy");
    case CareLevel::Attention: return tr("Needs attention");
    case CareLevel::Unknown:   break;
    }
    return {};
}

QString Format::careLevelIcon(int level) const
{
    // Material-Symbols ligatures (not translatable — they are font glyph names).
    switch (static_cast<CareLevel>(level)) {
    case CareLevel::Good:      return QStringLiteral("check_circle");
    case CareLevel::Attention: return QStringLiteral("warning");
    case CareLevel::Unknown:   break;
    }
    return {};
}

QString Format::notificationTitle(const QString &plantName) const
{
    return tr("%1 needs attention").arg(plantName);
}

QString Format::notificationBody(int quantity, int status) const
{
    const auto q = static_cast<Quantity>(quantity);
    const auto s = static_cast<CareStatus>(status);
    // The defining notification reframing: dry soil IS the "time to water" signal — phrase it as the
    // action, not a raw threshold breach (the whole point of monitoring is to water on need).
    if (q == Quantity::SoilMoisture && s == CareStatus::TooLow)
        return tr("Time to water — the soil is too dry.");
    const QString name = quantityLabel(quantity);
    if (name.isEmpty())
        return {};
    switch (s) {
    case CareStatus::TooLow:  return tr("%1 is too low.").arg(name);
    case CareStatus::TooHigh: return tr("%1 is too high.").arg(name);
    case CareStatus::Ideal:   // not an alerting state — no notification body
    case CareStatus::Unknown: break;
    }
    return {};
}

} // namespace klr
