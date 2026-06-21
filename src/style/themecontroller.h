// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tokens.h"

#include <QtGui/QColor>
#include <QtCore/QObject>
#include <QtQml/qqmlregistration.h>

namespace klr {

// The single source of colour/type/spacing the whole UI binds to (exposed to QML as
// `Theme`). Token *values* live in tokens.h (unit-tested); this is the live, scheme-
// aware view of them. A self-contained QML_SINGLETON — the engine default-constructs
// it (no services to inject, unlike AppContext), so no create() factory is needed.
//
// Colours change with the active ColorScheme (NOTIFY colorsChanged); type/spacing/
// radius are scheme-independent (CONSTANT). Semantic colours (good/warn/bad/ai) live
// here, the one colour owner; enum/status -> colour *mapping* is Format's job.
// `colorAI` (Cyan) is the AI + hydration/sensor accent — use it for nothing else
// (the design system brand rule).
class ThemeController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(ColorScheme colorScheme READ colorScheme WRITE setColorScheme NOTIFY colorsChanged)
    Q_PROPERTY(bool darkActive READ darkActive NOTIFY colorsChanged)
    Q_PROPERTY(FormFactor formFactor READ formFactor WRITE setFormFactor NOTIFY formFactorChanged)

    // Colours (re-resolve on scheme change).
    Q_PROPERTY(QColor colorBackground READ colorBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorSurface READ colorSurface NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorCard READ colorCard NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorCardBorder READ colorCardBorder NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorText READ colorText NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorTextVariant READ colorTextVariant NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorOutline READ colorOutline NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorPrimary READ colorPrimary NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorOnPrimary READ colorOnPrimary NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorGood READ colorGood NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorWarn READ colorWarn NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorBad READ colorBad NOTIFY colorsChanged)
    Q_PROPERTY(QColor colorAI READ colorAI NOTIFY colorsChanged)

    // Type ramp (Montserrat display/headline, Inter body/label — the design system).
    Q_PROPERTY(QString fontDisplay READ fontDisplay CONSTANT)
    Q_PROPERTY(QString fontBody READ fontBody CONSTANT)
    // The Material Symbols icon font family (used by the Icon component).
    Q_PROPERTY(QString fontIcon READ fontIcon CONSTANT)
    Q_PROPERTY(int fontSizeDisplay READ fontSizeDisplay CONSTANT)
    Q_PROPERTY(int fontSizeHeadline READ fontSizeHeadline CONSTANT)
    Q_PROPERTY(int fontSizeTitle READ fontSizeTitle CONSTANT)
    Q_PROPERTY(int fontSizeSubtitle READ fontSizeSubtitle CONSTANT)
    Q_PROPERTY(int fontSizeBodyLarge READ fontSizeBodyLarge CONSTANT)
    Q_PROPERTY(int fontSizeBody READ fontSizeBody CONSTANT)
    Q_PROPERTY(int fontSizeLabel READ fontSizeLabel CONSTANT)
    Q_PROPERTY(int fontSizeCaption READ fontSizeCaption CONSTANT)

    // Spacing (8px rhythm — the design system). Note `sm` (12) > `base` (8) per the spec.
    Q_PROPERTY(int spacingXs READ spacingXs CONSTANT)
    Q_PROPERTY(int spacingBase READ spacingBase CONSTANT)
    Q_PROPERTY(int spacingSm READ spacingSm CONSTANT)
    Q_PROPERTY(int spacingMd READ spacingMd CONSTANT)
    Q_PROPERTY(int spacingLg READ spacingLg CONSTANT)
    Q_PROPERTY(int spacingXl READ spacingXl CONSTANT)
    Q_PROPERTY(int gutter READ gutter CONSTANT)
    Q_PROPERTY(int marginCompact READ marginCompact CONSTANT)
    Q_PROPERTY(int marginPage READ marginPage CONSTANT)

    // Radius.
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT)
    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(int radiusMd READ radiusMd CONSTANT)
    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT)
    Q_PROPERTY(int radiusFull READ radiusFull CONSTANT)

    // Elevation & depth (the design system). Fixed Dark-Emerald tints in both schemes
    // (see tokens.h) — CONSTANT, not scheme-derived. `colorShadow` is the soft ambient
    // card/dialog shadow; `colorBackdropScrim` dims the backdrop behind a modal.
    Q_PROPERTY(QColor colorShadow READ colorShadow CONSTANT)
    Q_PROPERTY(QColor colorBackdropScrim READ colorBackdropScrim CONSTANT)
    Q_PROPERTY(int elevationOffsetY READ elevationOffsetY CONSTANT)
    Q_PROPERTY(int elevationBlur READ elevationBlur CONSTANT)

public:
    enum class ColorScheme { Light, Dark, Auto };
    Q_ENUM(ColorScheme)
    enum class FormFactor { Phone, Tablet, Desktop };
    Q_ENUM(FormFactor)

    explicit ThemeController(QObject *parent = nullptr);

    ColorScheme colorScheme() const { return m_scheme; }
    void setColorScheme(ColorScheme s);
    bool darkActive() const;
    FormFactor formFactor() const { return m_formFactor; }
    void setFormFactor(FormFactor f);

    // Pure width->form-factor mapping the shell binds `formFactor` to (the thresholds
    // live here, in tested C++, not as QML magic numbers). Breakpoints:
    // < 600 -> Phone, < 1000 -> Tablet, >= 1000 -> Desktop. The desktop product's minimum
    // window width keeps it Tablet/Desktop; Phone is reached on the mobile build.
    Q_INVOKABLE FormFactor formFactorForWidth(int width) const;

    QColor colorBackground() const { return color(&tokens::Palette::background); }
    QColor colorSurface() const { return color(&tokens::Palette::surface); }
    QColor colorCard() const { return color(&tokens::Palette::card); }
    QColor colorCardBorder() const { return color(&tokens::Palette::cardBorder); }
    QColor colorText() const { return color(&tokens::Palette::text); }
    QColor colorTextVariant() const { return color(&tokens::Palette::textVariant); }
    QColor colorOutline() const { return color(&tokens::Palette::outline); }
    QColor colorPrimary() const { return color(&tokens::Palette::primary); }
    QColor colorOnPrimary() const { return color(&tokens::Palette::onPrimary); }
    QColor colorGood() const { return color(&tokens::Palette::good); }
    QColor colorWarn() const { return color(&tokens::Palette::warn); }
    QColor colorBad() const { return color(&tokens::Palette::bad); }
    QColor colorAI() const { return color(&tokens::Palette::ai); }

    // Care-status -> semantic colour. The mapping lives here because Theme is the
    // one colour owner and these re-theme via colorsChanged. `status` is a klr::CareStatus
    // and `level` a klr::CareLevel (passed as int from QML): Ideal/Good -> good (green),
    // out-of-range / Attention -> warn (amber), Unknown -> a muted variant.
    Q_INVOKABLE QColor careStatusColor(int status) const;
    Q_INVOKABLE QColor careLevelColor(int level) const;

    // Sensor connectivity -> semantic colour. `liveness` is a klr::Liveness int:
    // Live -> good (green), Stale -> warn (amber), Offline -> bad (red). Re-themes via
    // colorsChanged; callers hide the dot when liveness < 0 (no sensor bound).
    Q_INVOKABLE QColor livenessColor(int liveness) const;

    // A distinct, reading-type accent for a quantity's chart line (`quantity` is a
    // klr::Quantity int): water/soil = blue, light = amber, temperature = red/orange,
    // etc. These are deliberately FIXED hues OUTSIDE the app palette (and never green, so
    // the line stays legible over the green ideal-range band) — WatchFlower used the
    // same idea. Scheme-independent: mid-tones chosen to read on light and dark.
    Q_INVOKABLE QColor quantityColor(int quantity) const;

    QString fontDisplay() const { return QStringLiteral("Montserrat"); }
    QString fontBody() const { return QStringLiteral("Inter"); }
    QString fontIcon() const { return QStringLiteral("Material Symbols Outlined"); }
    int fontSizeDisplay() const { return 48; }
    int fontSizeHeadline() const { return 32; }
    int fontSizeTitle() const { return 24; }
    int fontSizeSubtitle() const { return 20; }
    int fontSizeBodyLarge() const { return 18; }
    int fontSizeBody() const { return 16; }
    int fontSizeLabel() const { return 14; }
    int fontSizeCaption() const { return 12; }

    int spacingXs() const { return 4; }
    int spacingBase() const { return 8; }
    int spacingSm() const { return 12; }
    int spacingMd() const { return 24; }
    int spacingLg() const { return 48; }
    int spacingXl() const { return 80; }
    int gutter() const { return 24; }
    int marginCompact() const { return 16; }
    int marginPage() const { return 64; }

    int radiusSm() const { return 4; }
    int radius() const { return 8; }
    int radiusMd() const { return 12; }
    int radiusLg() const { return 16; }
    int radiusFull() const { return 9999; }

    QColor colorShadow() const
    {
        return QColor(qRed(tokens::kShadowTint), qGreen(tokens::kShadowTint),
                      qBlue(tokens::kShadowTint), tokens::kShadowAlpha);
    }
    QColor colorBackdropScrim() const
    {
        return QColor(qRed(tokens::kShadowTint), qGreen(tokens::kShadowTint),
                      qBlue(tokens::kShadowTint), tokens::kScrimAlpha);
    }
    int elevationOffsetY() const { return tokens::kElevationOffsetY; }
    int elevationBlur() const { return tokens::kElevationBlur; }

signals:
    void colorsChanged();
    void formFactorChanged();

private:
    // Whether the *effective* scheme (resolving Auto against the OS) is dark.
    bool effectiveDark() const;
    const tokens::Palette &palette() const { return effectiveDark() ? tokens::kDark : tokens::kLight; }
    QColor color(QRgb tokens::Palette::*member) const
    {
        return QColor::fromRgb(palette().*member | 0xff000000u);
    }

    ColorScheme m_scheme { ColorScheme::Light };
    FormFactor m_formFactor { FormFactor::Desktop };
};

} // namespace klr
