# ADR 0015 — Light judged as Daily Light Integral (DLI)

**Status:** accepted · **Builds on:** ADR 0006 (readings/buckets/charts),
ADR 0008 (units/`SettingsStore`), ADR 0009 (care status & thresholds).

ADR 0009 judged light on its 3-day **peak lux** against the catalog's `lux` range. That is
horticulturally wrong for the **upper** bound: the catalog `lux` max denotes the bright end of a
plant's *sustained* preferred environment (mostly 20k–60k lux vs ~100k for full sun), not a
never-exceed instant — so testing the daily peak against it flags **every** plant that ever catches
~1 h of direct sun as "too bright" (a low-light, partly-shaded balcony reproduces this for every plant).
The biologically meaningful measure is the accumulated daily **dose** — the Daily Light Integral —
which the catalog already carries as the `Light MIN/MAX (mmol)` column (mmol·m⁻²·day⁻¹), the column
ADR 0009 deliberately left unmapped. This phase routes the light verdict through DLI.

**Decisions (this phase):** light is judged on DLI for **both** bounds (the most accurate
plant measure — a brief bright spot can hit a high peak yet accumulate too little total light, which
peak misreads); the lux-peak verdict is dropped entirely. And the computed dose is **surfaced** on
the care tab as a "Daily light" readout, not just used internally.

> **Amendment (min-only — supersedes "both bounds" above).** Switching to DLI fixed the *statistic*
> but **not** the over-flagging: with the dose judged against the catalog `mmol` **max**, every plant
> getting real daylight still read "too high." Investigation (web-sourced, recorded below) showed the
> catalog `mmol` maxima cluster at **4–8 mol·m⁻²·day⁻¹**, which is **6–10× below physical outdoor DLI**
> (full sun ≈ 30–60 mol·m⁻²·day⁻¹). For the many *outdoor* species in the catalog (shrubs, trees) that
> cannot be a damage ceiling — it is the top of an **ideal** band, "more light than strictly needed,"
> not "harmful." Corroborating: the device reports only instantaneous **lux** (never mmol — the BLE
> frame is a `uint32` lux), and the open-source ecosystem that uses this data (Home Assistant's `plant`
> integration) is **min-focused** — it flags when the daily *peak* never reaches `min_brightness`.
> **Decision:** light is judged **MIN-ONLY** — `evaluateDli` collapses `TooHigh`→`Ideal`, so a dose
> above max reads "ample" and only a dose below min ("not enough daily light") flags. Genuine
> over-light damage is species/heat/water-specific and not encoded here, so no max-bound alarm is
> attempted. The `Dli` range's max survives only as a **display** bound (metric-bar fraction).
> *(Sources: Apogee lux↔PPFD; HA `plant` integration docs; ChrisScheffler/miflora BLE wiki.)*

## Decisions

1. **A new, *derived* `Quantity::Dli`.** Appended **last** in the `Quantity` enum (so every measured
   quantity's persisted ordinal stays stable; `kQuantityCount` re-anchors to it). `Dli` is derived —
   **never decoded, stored as a reading, or broadcast**. It exists only as (a) a `CareRange`/threshold
   key seeded from the catalog mmol column, and (b) a synthesized care verdict + readout computed from
   the day's illuminance. The exhaustive `Quantity` switches handle it: `canonicalUnit` → `Unit::None`
   (no stored unit), `Format::label` → "Daily light", `ThemeController::quantityColor` → the amber
   light family, `backuptokens::toToken` → "Dli" (so a DLI threshold override survives backup/restore).

2. **No new `Unit`.** DLI is never stored as a reading and has no display-unit alternate, so adding a
   `Unit` enumerator would only force edits to more exhaustive `Unit` switches for nothing. Its unit
   string `mmol/m²/day` lives in `Format::dliUnitSymbol()`, shared by the readout and the thresholds
   editor. Scale is **mmol·m⁻²·day⁻¹ throughout**, matching the catalog column verbatim (zero scaling).

3. **Pure DLI math in `klr_core` (`carestatus.{h,cpp}`).**
   - `dliOf(readings, dayStart)` — trapezoid-integrate PPFD across the present samples in
     `[dayStart, dayStart+24h)` (lux→PPFD via the ADR 0008 daylight factor in `units.cpp`; µmol
     readings pass through), `÷1000` → mmol. Night falls out for free (lux≈0). `nullopt` when fewer
     than two in-window samples (no dose integrable). Clock-free — takes the explicit `dayStart`.
   - `meanDailyLightIntegral(readings, now, days = kDliWindowDays)` — mean over the last `days`
     **completed LOCAL days** (a plant's dose is a solar/human-day concept; `now`'s own local day is
     excluded as still in progress), skipping no-data days. `nullopt` until one completed day has
     data — the **partial-day guard** (this phase's analogue of the old fresh-sensor grace).
   - `evaluateDli(dose, range)` (thin over `evaluate`: `nullopt` → `Unknown`), `judgedOnDailyIntegral`,
     `kDliWindowDays = 3` (smooths weather; mirrors the old `kLightPeakDays`).
   - The dead peak path (`peakOf`/`evaluatePeak`/`judgedOnPeak`/`kLightPeakDays`/
     `kLightMinObservationMs`/`PeakWindow`) is **removed** — light is DLI now.

4. **One shared router (`statusForReading`).** The peak/extremes/current dispatch that was duplicated
   verbatim in `PlantCareModel::statusOf` and `PlantListModel::careOf` was lifted into one pure
   `klr_core` function taking the ranges, `now`, and an injected `ReadingWindowFn` (the models supply
   the `seriesForPlant` fetch; `klr_core` stays repository-free). Light's branch computes
   `meanDailyLightIntegral` and judges it against the **`Dli`** range — not the lux range. No `Dli`
   range, or no full day yet ⇒ `Unknown`.

5. **The catalog seeds both light columns.** `idealRanges` maps the mmol column to a `Quantity::Dli`
   range **and keeps** the lux `Illuminance` range. The lux range survives only as a **display range**
   for the metric/gradient bar; it drives no verdict. pH stays unmapped.

6. **Surfacing.** The editable light threshold in the care-thresholds pane is the **DLI dose row**
   (`Quantity::Dli` replaces `Illuminance` in `kQuantities`; mmol, no unit conversion, overridable,
   reset-to-species works). On the plant-detail **Care** tab, `PlantCareModel` appends a synthesized
   "Daily light" row (the dose + its verdict + a gradient bar) whenever a light sensor is bound and a
   `Dli` range exists; the **instantaneous lux row stays** for reference (value + bar) but its verdict
   pill is **neutralized** (`statusOf` returns `Unknown` for instantaneous light) so the dose alone
   carries the light health verdict. No QML change — the care repeater is data-driven.

## Consequences

- The headline fix (with the min-only amendment): an hour of direct sun — and indeed *any* ample
  daily dose, including one above the catalog max — reads **Ideal**, never TooHigh; a dim corner
  (dose below min) reads **TooLow** after one full day; a fresh sensor / partial first day reads
  Unknown. Light no longer raises a "too much" alarm at all.
- A plant whose catalog row has a `lux` range but a blank `mmol` column (rare) is not light-judged —
  acceptable; DLI is the light judgment and the dose can still be set manually in the thresholds pane.
- The verdict lags up to ~a day (it is judged on completed days); `kDliWindowDays = 3` smooths weather.

## Implementation order (one commit each, `ctest` green at each)

1. Extract the shared `statusForReading` router (pure refactor, no behaviour change).
2. Add `Quantity::Dli` + the exhaustive-switch ripple (no DLI judgment yet).
3. `dliOf` + `meanDailyLightIntegral` + `evaluateDli` + `kDliWindowDays` (pure, literal-series tests).
4. Seed the `Dli` range from the catalog mmol column (keep the lux range).
5. Route light through DLI in the router; remove the dead peak path; rewrite the light scenarios.
6. DLI threshold row + the synthesized "Daily light" care-tab readout.
7. This ADR.
8. **Min-only amendment:** `evaluateDli` collapses `TooHigh`→`Ideal`; route the `Dli` reading row
   through `evaluateDli` in `statusForReading` (so the readout pill matches); rewrite the sustained-
   high-dose scenarios to expect Ideal; this amendment + the README interpretation update.

## Tests

`test_carestatus` (dliOf trapezoid/edge/lux-conversion, completed-day mean, partial-day guard,
`evaluateDli`, router dispatch), `test_catalogthresholds` (the Dli range seeds with the mmol bounds),
`test_plantcaremodel` (1 h sun → Ideal; sustained high dose → Ideal [min-only]; dim → TooLow; withheld until a full day;
the Daily-light readout + neutralized instantaneous row), `test_plantlisthealth` (the list rollup
discriminates a sun-lover from a shade plant on the dose), `test_carethresholdsmodel` (the DLI row in
mmol with no unit conversion + override round-trip), `test_theme`/`test_units`/`test_backuptokens`
(the `Dli` switch arms). All pure / parity-tested; no judgment logic in QML/JS.
