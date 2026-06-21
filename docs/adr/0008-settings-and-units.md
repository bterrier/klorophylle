# ADR 0008 — Settings & units: a declarative preference layer + display-unit conversion

**Status:** accepted · **Builds on:** ADR 0002 (QML-singleton injection),
ADR 0004 (persistence baseline), ADR 0006 (readings/charts), ADR 0007 (`Theme`/`Format`).

This phase gives the app its first persisted preferences and the first unit choices. Until now nothing was
remembered across launches (the colour-scheme combo wrote `Theme.colorScheme` and lost it on
exit) and every reading showed only its canonical unit (°C, lux, hPa). This ADR records how the
preference layer is structured and where unit conversion happens. **Scope:** temperature °C/°F,
illuminance lux/µmol, pressure hPa/inHg/mmHg, and a persisted
colour scheme; locale/language switching is **deferred** (no i18n infra yet).

## Decisions

1. **Preferences are device-local UI state — QSettings, not SQLite.** Display preferences (units,
   colour scheme) are per-install and have no place in the sync-ready, change-logged SQLite schema:
   a user on °F at the desktop and °C on a phone is correct, not a conflict to reconcile. So they
   live in **QSettings**, leaving the schema (and its `change_log`) untouched — no migration. This
   matches WatchFlower and the settings brief ("…or a `QSettings` wrapper").

2. **A small injected key/value seam, faked for tests.** A pure `IKeyValueStore` interface
   (`value`/`setValue`) lives in `klr_core` with an `InMemoryKeyValueStore` fake; the QSettings-backed
   `QSettingsKeyValueStore` impl lives in `klr_persistence` (the storage layer). The interface is in
   core so `klr_style`'s `SettingsStore` can depend on it without reaching into storage — layering
   stays acyclic and downward (`klr_core → klr_style`; the concrete impl is injected from `app/`).

3. **`SettingsStore` is the one declarative preference singleton.** Exposed to QML as `Settings`
   (in `klr_style`, beside `Theme`/`Format`). One typed, tested accessor per preference over the
   injected `IKeyValueStore`, so read/write/default can't drift. Constructor-injected like
   `AppContext` (`create()` + `s_instance`, **no `getInstance()`**, ADR 0002). Preferences surface as
   **int** properties (matching QML `ComboBox.currentIndex`) that map 1:1 to the `klr` unit enums;
   `displayUnits()` is the C++ accessor the view-models read. Out-of-range values are clamped so a
   stale persisted int can never select a bogus enum.

4. **Conversion is pure and lives at the display boundary; storage stays canonical.** `klr_core`
   gains `units.{h,cpp}`: the `TemperatureUnit`/`IlluminanceUnit`/`PressureUnit` enums, a
   `DisplayUnits` struct (defaults == canonical), `convert(value, Unit from, Unit to)` (an affine
   table), and `displayUnit(Quantity, DisplayUnits)`. `formatValue` gains a `(Reading, DisplayUnits)`
   overload that converts then formats with the display unit's symbol; the canonical
   `formatValue(Reading)` delegates with default `DisplayUnits`, so existing call sites and tests are
   unchanged. **Readings are never re-stored** — a `-99`-free, `std::optional`-correct value is
   converted only when shown. All pure, covered by `test_units`.

5. **Temperature exact, pressure exact, illuminance approximate.** °C↔°F and hPa↔inHg/mmHg are exact.
   **lux↔µmol is a documented daylight approximation** (PPFD µmol ≈ lux × 0.0185) — the factor is
   spectrum-dependent, so it is an at-a-glance display aid, not a lab figure. Flagged here so a future
   per-light-source refinement has a home.

6. **The view-models re-render on a unit change.** `LiveReadingsModel` and `PlantCareModel` are
   injected with `SettingsStore` and format their `ValueTextRole`/`UnitRole` through `displayUnits()`.
   On `unitsChanged` the live model re-emits `dataChanged`; the care model `refresh()`es and
   re-loads any open history chart. `PlantCareModel` converts each history sample to the display unit
   **before** handing it to the (unit-agnostic) `SeriesModel`, so the QtGraphs value axis renders in
   the chosen unit with no change to `SeriesModel` or stored data.

7. **`SettingsStore` drives `Theme`, not the reverse.** The persisted colour-scheme *choice* lives on
   `SettingsStore`; `ThemeController` stays the live colour authority and free of persistence. A
   one-way QML `Binding { target: Theme; property: "colorScheme"; value: Settings.colorScheme }` in
   `Main.qml` applies the choice at startup and on change. `SettingsScreen` writes `Settings`.

8. **About-page metadata via a `BuildInfo` singleton.** A self-contained `BuildInfo` QML singleton
   (`klr_gui`, links `klr_version`) exposes app name/version (from the generated `version.h` — the
   single source of version truth), Qt runtime version, build type, license, and source URL, so the
   About screen carries **no hardcoded version**. (Landed alongside this phase as the "fill the About page"
   task; not a units concern but shares this ADR's commit series.)

## Implementation order (one commit each, tests green at each)

1. `klr_core` `units.{h,cpp}` (`convert`/`displayUnit`/`DisplayUnits`) + the `formatValue` overload +
   Unit-enum alternates + `test_units`.
2. `IKeyValueStore` + `InMemoryKeyValueStore` (core); `QSettingsKeyValueStore` (persistence).
3. `SettingsStore` (`Settings`) + `test_settingsstore`; wire `s_instance` in `main.cpp`.
4. Inject `SettingsStore` into `LiveReadingsModel`/`PlantCareModel` (unit-aware format + history
   conversion + re-render); `test_plantcaremodel` gains a °C→°F case.
5. QML: `Main.qml` `Theme`←`Settings` binding + `SettingsScreen` Units section & persisted scheme.
6. `BuildInfo` singleton + the filled-in `AboutScreen`.
7. This ADR.

## Not done in this phase (follow-ups)

- **Locale/language selection** and number/date formatting — deferred until i18n infra exists.
- **Care-status colours / threshold seeding** and `AggregationPolicy::Average` behind a setting
  — `Format`/`SettingsStore` provide the seam; the judgment lands with the care-status feature.
- A richer settings surface (notifications/reminders) and per-plant overrides.
- The lux↔µmol factor is a single daylight approximation; a per-light-source/spectrum refinement is
  a future option.
