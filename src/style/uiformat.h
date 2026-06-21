// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

// File is uiformat.h (not format.h) to avoid colliding with klr_core/format.h on the
// include path; the QML-facing type is still `Format`.
namespace klr {

// Presentation seam exposed to QML as `Format`. Today a thin QML face over the
// unit-tested core formatters (klr_core/format.h) so a binding can label a quantity or
// a unit without reaching into a model role. It GROWS here: enum/status -> label +
// theme-resolved colour and unit conversions land on this singleton, keeping
// all such logic in tested C++. Semantic colour
// *tokens* live on Theme (the colour owner); Format will own the *mapping* of a status
// to one of those tokens. Self-contained, so the engine default-constructs it.
class Format : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Format)
    QML_SINGLETON

public:
    explicit Format(QObject *parent = nullptr) : QObject(parent) {}

    // `quantity`/`unit` are the klr::Quantity / klr::Unit enum values (passed as int
    // from QML). Out-of-range yields an empty string.
    Q_INVOKABLE QString quantityLabel(int quantity) const;
    Q_INVOKABLE QString unitSymbol(int unit) const;

    // Care-status / plant-health -> short label. `status` is a klr::CareStatus and
    // `level` a klr::CareLevel (int from QML). The colour mapping is Theme's job (it owns
    // the live palette); the label lives here so QML carries no status text.
    Q_INVOKABLE QString careStatusLabel(int status) const;
    Q_INVOKABLE QString careLevelLabel(int level) const;

    // Plant-health level -> Material-Symbols glyph (the compact status badge): the
    // mapping lives here like the label/colour mappings so QML carries no glyph names.
    Q_INVOKABLE QString careLevelIcon(int level) const;

    // Notification text. Title is the headline (the plant's name + a nudge); body is
    // the one-line condition for a quantity that crossed its threshold. `quantity` is a
    // klr::Quantity, `status` a klr::CareStatus (only the alerting TooLow/TooHigh produce a
    // body). The defining reframing: a dry-soil alert reads as "time to water", not a raw
    // threshold breach — sensors tell you WHEN to water (ADR 0016). Tested in test_format.
    Q_INVOKABLE QString notificationTitle(const QString &plantName) const;
    Q_INVOKABLE QString notificationBody(int quantity, int status) const;
};

} // namespace klr
