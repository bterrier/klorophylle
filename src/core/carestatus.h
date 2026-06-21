// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reading.h"

#include <QtCore/QList>

#include <functional>
#include <optional>
#include <span>

// "Is my plant healthy?" — the pure care-status judgment. A reading is classified
// against the plant's ideal range for its quantity; the per-quantity verdicts roll up
// to one plant-level health level. All pure (no DB/BLE/QML, clock-free) and unit-tested
// with literal inputs — the judgment lives here, never in QML/JS.
//
// Ranges are expressed in the CANONICAL unit of the quantity (°C, lux, %, µS/cm — see
// canonicalUnit()), exactly as readings are stored, so evaluate() needs no conversion:
// the user's display-unit preference only affects how a value is *shown*, never
// how it is judged.
namespace klr {

// How one reading sits relative to its plant's ideal range for that quantity.
enum class CareStatus {
    Unknown, // no value, or no threshold to judge against
    TooLow,
    Ideal,
    TooHigh,
};

// One plant's ideal range for one quantity — the single mutable owner of "what this
// plant alerts on" (the "active thresholds"). Seeded from the
// catalog species (idealRanges()) but overridable. An unset bound (nullopt) means "no
// limit on that side"; a range with neither bound set judges nothing (Unknown).
struct CareRange {
    Quantity quantity {};
    std::optional<double> min {};
    std::optional<double> max {};

    bool isSet() const { return min.has_value() || max.has_value(); }
    bool operator==(const CareRange &) const = default;
};

// Classify a canonical-unit value against a range. Absent value or unset range ->
// Unknown; below min -> TooLow; above max -> TooHigh; otherwise Ideal. A pure numeric
// comparator — it does NOT know whether the quantity is meaningful instantaneously
// (see judgedInstantaneously); callers gate on that.
CareStatus evaluate(std::optional<double> value, const CareRange &range);

// Whether a status is one the user should be told about: out of range either way. The
// neutral states (Ideal, Unknown) are not alerting. The single predicate the notification
// debounce is built on.
bool isAlerting(CareStatus s);

// Whether a status CHANGE warrants a notification: fire only on the TRANSITION into an
// alerting state (previous not alerting, current alerting), so a plant that stays TooLow
// across many advertisements is announced once, not once per sample. Recovery (alerting →
// Ideal) is intentionally silent for now (ADR 0016). Pure — the AlertController owns the
// previous-status memory; this just decides given the pair.
bool shouldNotify(CareStatus previous, CareStatus current);

// How long a transient excursion (a cold night, a hot afternoon) keeps alerting. Some
// quantities — temperature — can harm a plant briefly and then recover, so a verdict
// based only on the current value would miss it; instead we judge the min/max over this
// trailing window, so a check next morning still surfaces last night's cold snap. The
// excursion clears on its own once it ages out of the window. Tunable in one place.
inline constexpr qint64 kExtremesWindowMs = 24LL * 60 * 60 * 1000; // 24h

// Whether a quantity is judged on its recent EXTREMES (min & max over kExtremesWindowMs)
// rather than its current value: air/soil temperature, where a transient excursion still
// matters after the reading recovers. (Light is handled separately, by peak; everything
// else is judged on its current value.)
bool judgedOnRecentExtremes(Quantity q);

// The lowest and highest present value across a set of readings (nullopt when none).
struct Extremes {
    std::optional<double> min {};
    std::optional<double> max {};
};
Extremes extremesOf(std::span<const Reading> readings);

// Judge a quantity from its recent Extremes: the window's min below range.min ⇒ TooLow,
// its max above range.max ⇒ TooHigh, else Ideal. So an excursion anywhere in the window
// is flagged even if the latest reading is back in range. If BOTH bounds were breached
// in the window (a cold night and a hot afternoon), TooLow is reported. No data / unset
// range ⇒ Unknown.
CareStatus evaluateExtremes(const Extremes &extremes, const CareRange &range);

// --- Daily Light Integral ----------------------------------------------------------
// The horticulturally correct way to judge light: its accumulated daily DOSE, not a peak.
// The catalog's lux max is a *sustained* environment bound, so testing a daily peak against
// it over-flags any plant that ever catches direct sun. DLI integrates the day's light
// instead and judges it against the catalog's mmol·m⁻²·day⁻¹ column (Quantity::Dli).
//
// Light is judged MIN-ONLY: "is the plant getting enough daily light?", never "too much?".
// The catalog mmol max is the top of an *ideal* band, not a tolerance ceiling — for the many
// outdoor species it sits far below physical outdoor DLI (4–8 vs 30–60 mol·m⁻²·d⁻¹ in full
// sun), so above-max means "ample", not harm, and the open-source ecosystem that uses this
// data (the Home Assistant `plant` integration) is likewise min-focused. Genuine over-light
// damage is species/heat/water-specific and not encoded here. See ADR 0015.

// Window (in completed local days) the DLI verdict averages over, to smooth weather — one
// overcast day must not flip a plant's light status. Mirrors kLightPeakDays; tuned here.
inline constexpr int kDliWindowDays = 3;

// Whether a quantity is judged on its accumulated Daily Light Integral: light (illuminance
// / PPFD). The verdict is produced against the plant's Quantity::Dli range (mmol·m⁻²·day⁻¹),
// not the lux range — which survives only as a display range for the metric bar.
bool judgedOnDailyIntegral(Quantity q);

// The Daily Light Integral over [dayStart, dayStart+24h): trapezoid-integrate PPFD across
// consecutive present in-window samples (lux→PPFD via the documented daylight factor in
// units.cpp; readings already in µmol pass through), in mmol·m⁻²·day⁻¹. Night falls out for
// free (lux≈0 contributes ≈0). nullopt when fewer than two present samples fall in the day
// (no dose can be integrated). Readings must be oldest-first, as seriesForPlant returns
// them. Pure, clock-free.
std::optional<double> dliOf(std::span<const Reading> readings, const QDateTime &dayStart);

// The mean DLI over the last `days` COMPLETED local days (skipping days with no integrable
// dose). nullopt when no completed day has data yet — the partial-day guard that withholds
// a light verdict until a full day has been observed (the DLI analogue of evaluatePeak's
// fresh-sensor grace). `now`'s local day is "today" and is excluded as still in progress.
std::optional<double> meanDailyLightIntegral(std::span<const Reading> readings,
                                             const QDateTime &now, int days = kDliWindowDays);

// Judge a light dose against the plant's DLI range, MIN-ONLY: nullopt (no full day yet) →
// Unknown; below min → TooLow; at/above min → Ideal (a dose above max is "ample", never
// TooHigh — see the min-only rationale above). Wraps evaluate(), collapsing TooHigh→Ideal.
CareStatus evaluateDli(std::optional<double> dose, const CareRange &range);

// Fetches a plant's recent readings for one quantity over [from, to] (every binding, so
// the series follows the plant across sensor swaps). The view-models supply this over
// IReadingRepository::seriesForPlant; klr_core stays repository-free. Used by
// statusForReading for the quantities judged on a recent window (light, temperature).
using ReadingWindowFn =
    std::function<QList<Reading>(Quantity quantity, const QDateTime &from, const QDateTime &to)>;

// The ONE place that knows how each quantity is judged — the dispatch shared by the
// plant-list and plant-care models (it used to be duplicated verbatim in both). Looks up
// `current`'s range in `ranges`; light is judged on its recent window, temperature on its
// recent extremes, everything else on the current value. `now` anchors the window starts;
// `window` pulls the recent same-quantity series (the current reading is folded in, so a
// single value is still judged when history is empty). No range for the quantity ⇒ Unknown.
CareStatus statusForReading(const Reading &current, std::span<const CareRange> ranges,
                            const QDateTime &now, const ReadingWindowFn &window);

// Plant-level health, rolled up across the per-quantity statuses.
enum class CareLevel {
    Unknown,   // nothing measured, or no thresholds set
    Good,      // at least one Ideal reading and nothing out of range
    Attention, // at least one quantity TooLow / TooHigh
};

// Worst-of rollup: any TooLow/TooHigh -> Attention; else any Ideal -> Good; else Unknown.
CareLevel rollup(std::span<const CareStatus> statuses);

// The range for `q` within a set, or nullopt if the set has none for it.
std::optional<CareRange> rangeFor(std::span<const CareRange> ranges, Quantity q);

} // namespace klr
