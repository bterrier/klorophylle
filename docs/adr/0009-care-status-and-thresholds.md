# ADR 0009 — Care status & thresholds: "is my plant healthy?"

**Status:** accepted · **Builds on:**
ADR 0004 (persistence baseline), ADR 0005 (plant↔sensor binding), ADR 0006 (readings/charts),
ADR 0007 (`Theme`/`Format`), ADR 0008 (units/`SettingsStore`).

This phase turns the logger-that-charts into a care **assistant**: each reading is judged against the
plant's ideal range, and the verdict surfaces on the reading, the plant list and the history chart.
This ADR records where that judgment lives, who owns the ranges, and how it is shown.

## Decisions

1. **The judgment is a pure `klr_core` function.** `carestatus.{h,cpp}` adds `CareStatus`
   (`Unknown`/`TooLow`/`Ideal`/`TooHigh`), the `CareRange` value type (`Quantity` + optional
   `min`/`max`), `evaluate(value, range) -> CareStatus`, a plant-level `CareLevel`
   (`Unknown`/`Good`/`Attention`) with a worst-of `rollup(span<CareStatus>)`, and a `rangeFor`
   helper. All pure, clock-free, unit-tested with literal inputs (`test_carestatus`) — the judgment
   never lives in QML/JS. Bounds are **inclusive** (exactly-min is `Ideal`).

2. **Ranges are judged in the canonical unit.** Readings are stored canonically (°C, lux, %, µS/cm;
   ADR 0006) and ranges are stored canonically, so `evaluate()` needs no conversion — the
   display-unit preference changes only how a value is *shown*, never how it is judged. The same
   value is `TooLow` whether the user reads °C or °F.

2a. **Light is judged on its daily PEAK, not its instantaneous value — it is diurnal.**
   `judgedOnDailyPeak(Quantity)` (`klr_core`) returns true for `Illuminance`/`Ppfd`; `statusOf`/
   `healthOf` then evaluate the **brightest reading of the last `kLightPeakDays` days** (3 by default)
   against the lux range (peak below min ⇒ never bright enough; above max ⇒ too harsh) instead of the
   current value. Illuminance falls to ~0 every night, so judging the instant value against a daytime
   minimum is the classic "too low at night" false alarm; the peak is night-immune by construction.
   A multi-day window (not a single day) means a run of overcast days doesn't read like permanent
   shade — `kLightPeakDays` (`klr_core`) tunes it in one place. The current value is still *shown*;
   only the verdict uses the peak. The peak is computed from the reading history (`seriesForPlant`
   over `[now−kLightPeakDays·d, now]`, clipped to the binding windows so a swap is handled).
   **Fresh-sensor grace:** a `TooLow` light verdict is withheld (→ `Unknown`) until the readings
   span at least `kLightMinObservationMs` (~a day), so a just-paired sensor — or one paired at night
   — hasn't yet missed a daytime peak and so doesn't false-alarm; `TooHigh`/`Ideal` need no wait
   (one bright reading already proves them). This is the pure `evaluatePeak(PeakWindow, range)` over
   `peakOf(readings)` (both in `klr_core`, unit-tested), used by `statusOf`/`healthOf`. The
   catalog also carries a **"Light mmol"** Daily-Light-Integral column; a DLI roll-up against it is a
   finer future refinement (decision below), but the peak is the pragmatic daytime check now.
   (WatchFlower surfaced light as a gauge rather than a health verdict.)

2b. **Temperature is judged on its recent EXTREMES — a transient excursion still alerts.**
   `judgedOnRecentExtremes(Quantity)` (`klr_core`) is true for air/soil temperature; `statusOf`/
   `healthOf` then judge the **min and max over `kExtremesWindowMs`** (24h): the window's min below
   `range.min` ⇒ `TooLow`, its max above `range.max` ⇒ `TooHigh`, else `Ideal` (`evaluateExtremes`
   over `extremesOf`, both pure/tested). A cold snap can damage a plant briefly and then recover, so
   judging only the current value would miss it — this way a check next morning still surfaces last
   night's dip even though the reading is back in range, and the excursion clears on its own once it
   ages out of the 24h window. No fresh-sensor grace is needed (an excursion only fires on a value
   actually recorded). If both bounds were breached in the window, `TooLow` is reported. The current
   value is still *shown*; only the verdict uses the window. (Both the peak and extremes windows
   **fold in the current reading**, so a single value is still judged when history is empty — a
   just-attached sensor whose first sample predates the binding window would otherwise read as no
   data.)

3. **One owner of ideal ranges, one of active thresholds — the dual-ownership fix.** The
   bundled **catalog species** is the immutable owner of *ideal* ranges: a pure
   `idealRanges(CatalogEntry) -> QList<CareRange>` (`klr_persistence/catalogthresholds.{h,cpp}`) maps
   the catalog-parsed fields to the five quantities the app both measures and stores canonically — soil
   moisture, soil conductivity, **air** temperature (the unit Flower Care broadcasts), air humidity
   and illuminance. The catalog's pH (no `Quantity`) and the **daily-light-integral** mmol column (a
   DLI, not the instantaneous PPFD of `Quantity::Ppfd`) are deliberately **not** mapped. The per-plant
   **`care_thresholds`** table is the single mutable owner of *active* thresholds; the sensor stores
   no limits.

4. **Thresholds persist in schema v3 behind the repository boundary.** `care_thresholds`
   (`plant_id`, `quantity`, `min_value`, `max_value`, PK `(plant_id, quantity)`, FK cascade) is the
   whole per-plant row-set keyed by `plantId` — matching the domain model. `ICareThresholdRepository`
   (`thresholdsFor`/`setRange`/`replaceAll`/`clear`) has an `InMemory*` fake and a `Sqlite*` impl,
   both passing the **same** behavioural suite (`test_carethresholdrepository`); a `setRange` with
   neither bound deletes the row (keeps the table sparse). Each mutation + its `change_log` row run in
   one transaction (the set is one syncable entity; the HLC reducer stays deferred).

5. **Seeding is the explicit, idempotent ideal→active sync — plus auto-recovery for empty sets.**
   Choosing a species (`addPlant` / `setSelectedPlantSpecies`) `replaceAll`s the plant's thresholds
   with the species' ideal ranges — a deliberate "use this species' ranges" act that overwrites a
   prior manual set. Clearing the species leaves the thresholds untouched. Editing is then free-form
   per quantity. **Additionally, `selectPlant` auto-recovers** a plant that has a species but an
   *empty* threshold set (a plant created before this feature, or before its species was set): it seeds from
   the species on open, so "no limit ⇒ use species values" needs no manual *Reset to species* click.
   The guard is strict — auto-recovery fires only when the set is empty, so a hand-edited set is
   never clobbered. (`test_appcontext`)

6. **The editable thresholds live in a thin view-model.** `CareThresholdsModel` (`klr_gui`) shows one
   row per carable quantity, **in the user's display unit**, converting at the boundary so the
   stored value stays canonical; `setRange(quantity, minText, maxText)` parses + converts back,
   `resetToSpecies()` re-seeds. Surfaced on the **plant settings** subscreen with a *Reset to species*
   action. **A threshold cannot be cleared away:** blanking *one* bound is a valid "no limit on that
   side", but blanking *both* is rejected (a no-op that reverts the editor) so a quantity is never
   left unjudged — the user changes ranges, never deletes them; *Reset to species* is the only way
   back to defaults. (The repository's unset-deletes-the-row remains a low-level primitive, used by
   `replaceAll`/seeding, not reachable from the editor.) Covered by `test_carethresholdsmodel`.

7. **Status flows through the existing view-models — no new judgment in QML.**
   `PlantCareModel` gains a `StatusRole` (each current reading `evaluate`d against the cached ranges)
   and, in `loadHistory`, an **ideal-range band** converted to the display unit and handed to
   `SeriesModel` (new `hasBand`/`bandMin`/`bandMax`, folded into the value axis; a one-sided bound
   clamps to the axis edge). `PlantListModel` gains a `HealthRole` — the worst-of `rollup` across a
   plant's current readings, computed from the injected sensor/binding/reading/threshold repos (a
   plant-only `PlantListModel` still works, reporting `Unknown`). Tested in `test_plantcaremodel`,
   `test_seriesmodel`, `test_plantlisthealth`.

8. **Colour is `Theme`'s job, label is `Format`'s.** The status→colour mapping
   (`Theme.careStatusColor`/`careLevelColor`) lives on `ThemeController` because it is the one live
   colour owner and these re-theme via `colorsChanged` (`Ideal`/`Good` → `colorGood`, out-of-range /
   `Attention` → `colorWarn`, `Unknown` → muted). The status→label mapping
   (`Format.careStatusLabel`/`careLevelLabel`) lives on `Format`. QML carries neither colour nor text
   logic — a `StatusPill` binds to both. Out-of-range is **warn (amber)**, not bad (red): it is
   "requires attention", not an error. Covered by `test_theme`.

## Implementation order (one commit each, tests green at each)

1. `klr_core` `carestatus.{h,cpp}` (`evaluate`/`rollup`/`CareRange`) + `test_carestatus`.
2. `klr_persistence` `catalogthresholds.{h,cpp}` (`idealRanges`) + `test_catalogthresholds`.
3. Schema **v3** `care_thresholds` + `ICareThresholdRepository` (in-mem + SQLite) +
   `test_carethresholdrepository` + migration-test bump.
4. `PlantCareModel` `StatusRole` + `SeriesModel` band + `loadHistory` band + species seeding in
   `AppContext`; `test_plantcaremodel`/`test_seriesmodel` cases.
5. `CareThresholdsModel` (editable, display-unit) + `AppContext` wiring + `test_carethresholdsmodel`.
6. `PlantListModel` `HealthRole` + `Theme`/`Format` colour/label mappings + QML (plant-list pill,
   reading colours, thresholds editor, chart band) + `test_theme`/`test_plantlisthealth`.
7. This ADR.

## Not done in this phase (follow-ups)

- **Notifications/reminders:** firing on a *threshold transition* (debounced, not per-sample)
  and watering reminders. This phase provides the status + transitions to alert on.
- **`AggregationPolicy::Average`** behind a setting (still NewestWins; ADR 0005).
- **Daily Light Integral judging for light** (refining decision 2a): light is currently judged on its
  24h *peak* lux; a finer check accumulates the day's lux into a DLI and judges it against the
  catalog's "Light mmol" range (needs a lux→DLI conversion / per-light-source PPFD factor). The peak
  catches "never bright enough / too harsh"; the DLI would also catch "bright but too briefly".
- Soil-**pH** thresholds (no `Quantity::SoilPh` yet) and any custom, non-catalog quantity.
- Live plant-list health refresh on every advertisement (today it refreshes on navigation, threshold
  edits and species changes — not on each cached sample).
