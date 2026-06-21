// SPDX-License-Identifier: GPL-3.0-or-later
#include "themecontroller.h"

#include "carestatus.h" // klr_core: CareStatus / CareLevel
#include "liveness.h"   // klr_core: Liveness
#include "reading.h"    // klr_core: Quantity

#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>

namespace klr {

namespace {

// Register the bundled brand fonts once. They are variable TTFs (one per family) under
// the klr_style resources, so the family names resolve to "Montserrat" / "Inter" and
// font.weight selects the instance. No-op without a QGuiApplication (e.g. guiless unit
// tests) — QFontDatabase needs the GUI font system.
void ensureFontsRegistered()
{
    static bool done = false;
    if (done || !qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return;
    QFontDatabase::addApplicationFont(QStringLiteral(":/klr/fonts/Montserrat-VariableFont.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/klr/fonts/Inter-VariableFont.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/klr/fonts/MaterialSymbolsOutlined.ttf"));
    done = true;
}

} // namespace

ThemeController::ThemeController(QObject *parent)
    : QObject(parent)
{
    ensureFontsRegistered();

    // When the scheme is Auto, follow the OS light/dark preference live.
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this] {
            if (m_scheme == ColorScheme::Auto)
                emit colorsChanged();
        });
    }
}

void ThemeController::setColorScheme(ColorScheme s)
{
    if (s == m_scheme)
        return;
    m_scheme = s;
    emit colorsChanged();
}

void ThemeController::setFormFactor(FormFactor f)
{
    if (f == m_formFactor)
        return;
    m_formFactor = f;
    emit formFactorChanged();
}

ThemeController::FormFactor ThemeController::formFactorForWidth(int width) const
{
    if (width < 600)
        return FormFactor::Phone;
    if (width < 1000)
        return FormFactor::Tablet;
    return FormFactor::Desktop;
}

bool ThemeController::effectiveDark() const
{
    switch (m_scheme) {
    case ColorScheme::Light:
        return false;
    case ColorScheme::Dark:
        return true;
    case ColorScheme::Auto:
        if (auto *hints = QGuiApplication::styleHints())
            return hints->colorScheme() == Qt::ColorScheme::Dark;
        return false;
    }
    return false;
}

bool ThemeController::darkActive() const
{
    return effectiveDark();
}

QColor ThemeController::careStatusColor(int status) const
{
    switch (static_cast<CareStatus>(status)) {
    case CareStatus::Ideal:   return colorGood();
    case CareStatus::TooLow:
    case CareStatus::TooHigh: return colorWarn();
    case CareStatus::Unknown: break;
    }
    return colorTextVariant(); // no judgment -> muted, not alarming
}

QColor ThemeController::careLevelColor(int level) const
{
    switch (static_cast<CareLevel>(level)) {
    case CareLevel::Good:      return colorGood();
    case CareLevel::Attention: return colorWarn();
    case CareLevel::Unknown:   break;
    }
    return colorTextVariant();
}

QColor ThemeController::livenessColor(int liveness) const
{
    if (liveness == kConnectivityConnected)
        return colorAI(); // blue: a GATT connection is open to this device right now
    switch (static_cast<Liveness>(liveness)) {
    case Liveness::Live:    return colorGood();
    case Liveness::Stale:   return colorWarn();
    case Liveness::Offline: return colorBad();
    }
    return colorTextVariant(); // out-of-range (e.g. -1, no sensor) -> muted
}

QColor ThemeController::quantityColor(int quantity) const
{
    // Fixed reading-type hues (see header). Grouped so related quantities share a family
    // (all soil/water = blue, both temperatures = warm, both lights = amber). No greens.
    switch (static_cast<Quantity>(quantity)) {
    case Quantity::SoilMoisture:     return QColor(0x2E, 0x86, 0xDE); // blue (water)
    case Quantity::WaterTank:        return QColor(0x10, 0xAC, 0xC4); // cyan (water)
    case Quantity::AirHumidity:      return QColor(0x16, 0xA0, 0x97); // teal
    case Quantity::SoilConductivity: return QColor(0x8E, 0x5B, 0xD0); // violet (fertility)
    case Quantity::AirTemperature:   return QColor(0xE8, 0x4B, 0x3C); // red
    case Quantity::SoilTemperature:  return QColor(0xEE, 0x7A, 0x2E); // orange
    case Quantity::Illuminance:      return QColor(0xF2, 0xB6, 0x05); // amber (light)
    case Quantity::Ppfd:             return QColor(0xF2, 0x8C, 0x05); // deep amber (light)
    case Quantity::Pressure:         return QColor(0x5C, 0x6B, 0xC0); // indigo
    case Quantity::Co2:              return QColor(0x8D, 0x6E, 0x63); // brown
    case Quantity::Voc:              return QColor(0xA6, 0x4D, 0xC4); // purple
    case Quantity::Hcho:             return QColor(0xD8, 0x1B, 0x8C); // magenta
    case Quantity::Pm25:             return QColor(0x60, 0x7D, 0x8B); // slate
    case Quantity::Pm10:             return QColor(0x45, 0x5A, 0x64); // dark slate
    case Quantity::Radioactivity:    return QColor(0xC2, 0x18, 0x5B); // crimson
    case Quantity::Battery:          return QColor(0x78, 0x90, 0x9C); // blue-grey
    case Quantity::Dli:              return QColor(0xF2, 0xB6, 0x05); // amber (light, like Illuminance)
    }
    return QColor(0x4C, 0x8D, 0xF5); // sensible blue fallback (never green)
}

} // namespace klr
