# ADR 0016 — Care notifications

**Status:** accepted
**Builds on:** ADR 0009 (care status & thresholds), ADR 0011 (always-on sensor ingestion),
ADR 0008 (units/`SettingsStore`), ADR 0002 (QML-singleton injection).

ADR 0009 made the app *judge* a plant's health and ADR 0011 made readings advance the whole time the app runs,
but nothing yet **tells the user when to act** — they had to open the app and look. This phase closes the
loop: a desktop notification when a plant's care status crosses into an out-of-range state.

**The defining decision (re-framing the "watering reminder" half):** *"the whole point of
using sensors is to water the plant when needed, not on a regular schedule."* So this phase has **no
calendar/journal watering reminders** at all — the **soil-moisture-`TooLow` transition *is* the
"time to water" signal. That collapses it to a single mechanism (threshold-transition notifications),
needs **no** new domain field and **no** schema change, and the notification text phrases dry soil as
the action ("Time to water — the soil is too dry") rather than a raw threshold breach.

**Decisions (this phase).** Delivery backend: **freedesktop `org.freedesktop.Notifications`
over D-Bus** — keeps the app on `QGuiApplication` (no Qt Widgets, no tray icon), lighter than
`QSystemTrayIcon`; behind a testable interface so the mobile build can add per-platform backends. Preferences:
a **global enable** + a **global snooze deadline** (no per-plant/per-category state yet).

## Decisions

1. **Transition debounce as a pure rule (`klr_core`).** `isAlerting(CareStatus)` = `TooLow || TooHigh`;
   `shouldNotify(previous, current)` fires iff `current` is alerting and `previous` is not — the edge
   *into* a bad state. A plant that stays dry across many advertisements is announced **once**.
   Recovery (alerting → Ideal) is intentionally **silent** in v1. Unit-tested at the boundary.

2. **Shared care evaluation (`klr_gui/careevaluation.{h,cpp}`).** The binding/reading/range fetch +
   `statusForReading` dispatch that `PlantListModel::careOf` ran inline was extracted into
   `evaluatePlantCare(plant, repos…, now) -> PlantCareSnapshot { current, ranges, statuses, level }`,
   reused by the list health pill **and** the notification evaluator — so neither re-derives it (the
   per-quantity dispatch itself already lived once in `statusForReading`, ADR 0015). Behaviour-
   preserving for the list (`test_plantlisthealth` unchanged); `PlantCareModel` left untouched (its
   DLI-row logic is not disturbed this phase).

3. **`AlertController` (`klr_gui`, non-QML, testable).** Injected the plant/binding/reading/threshold
   repos + catalog + `Clock` + `SettingsStore` + an `INotificationSink`. `evaluate()` re-judges every
   plant at the clock's now, and for each `(plant, quantity)` notifies when `shouldNotify(previous,
   current)` holds, then updates the in-memory `previous` map. **Delivery** is gated by the global
   enable + the snooze deadline; the `previous` map is **primed even while muted**, so un-snoozing /
   re-enabling never replays a transition the user chose to skip. State is **in-memory**: a restart
   re-evaluates, so a still-dry plant re-announces on the first advertisement after launch (acceptable
   for an always-on monitor — that *is* "this plant still needs water"; persisting last-alerted is a
   documented follow-up). It evaluates **all** plants per ingest (same cost as the existing blanket
   `PlantListModel::refresh()`); a surgical "plants bound to this sensor" lookup can refine both later.

4. **`INotificationSink` seam + freedesktop backend.** The interface (`klr_gui/inotificationsink.h`,
   `notify(title, body)`) keeps `klr_gui` platform- and D-Bus-free and lets tests inject a recording
   fake. The one concrete impl, `FreedesktopNotificationSink`, lives in **`app/`** and is the only
   thing that links `Qt6::DBus`; it `asyncCall`s `org.freedesktop.Notifications.Notify` (fire-and-
   forget — if no notification server runs, the call simply does nothing).

5. **Notification text on `Format` (`klr_style`).** `notificationTitle(plantName)` ("%1 needs
   attention") and `notificationBody(quantity, status)` — with the **`SoilMoisture` + `TooLow` →
   "Time to water — the soil is too dry"** special case; other quantities use the generic "%1 is too
   low/high". No judgment or label text in QML/JS.

6. **Preferences on `SettingsStore` (`klr_style`).** `notificationsEnabled` (bool, default `true`)
   and `notificationsSnoozedUntilMs` (epoch ms, default `0`, clamped ≥ 0) — device-local, **out of the
   sync-ready SQLite schema** (QSettings-backed), following the `colorScheme`/`exportPeriodIndex`
   pattern. The snooze *deadline* (not a duration) is stored so it survives restart; `AppContext`
   computes it from the injected clock (`snoozeNotifications(hours)`).

7. **Wiring (`AppContext`, composition root).** `AppContext` takes the `AlertController*` and calls
   `evaluate()` inside the existing always-on ingest block (`if (wrote) { … }`, ADR 0011) — alerts are
   reading-driven. It exposes `snoozeNotifications(hours)` and a C++-formatted `notificationsSnoozedText`
   (empty unless currently snoozed) for the settings pane; the master enable binds directly to the
   `Settings` QML singleton. The composition root constructs the sink + controller and injects them.

## Consequences

- A plant whose soil dries past its `min` raises **one** "time to water" notification; it does not
  repeat while the plant stays dry, and re-fires only after it recovers and dries again.
- Light never raises a "too much" alarm (it inherits the min-only DLI judgment via the shared router,
  ADR 0015), so notifications don't reintroduce the over-flagging that change fixed.
- No notifications module is linked into `klr_gui`/`klr_core`; D-Bus is confined to the executable.
- Snooze is "don't bother me until T": a transition during the snooze window is skipped, not queued.

## Implementation order (one commit each, `ctest` green at each)

1. `isAlerting` / `shouldNotify` in `carestatus` (+ `test_carestatus`).
2. Extract `evaluatePlantCare`; refactor `careOf` onto it (+ `test_careevaluation`; list test stays green).
3. `INotificationSink` + `Format::notificationTitle/Body` (+ `test_format`).
4. `notificationsEnabled` + `notificationsSnoozedUntilMs` on `SettingsStore` (+ `test_settingsstore`).
5. `AlertController` (+ `test_alertcontroller`: one-per-transition, recovery+re-drop, disable/snooze
   suppress while priming, first-seen-dry fires, no-range silent).
6. `FreedesktopNotificationSink` in `app/`; link `Qt6::DBus` on the executable only.
7. Wire `AlertController` into `AppContext` (ingest call + `snoozeNotifications` + snooze text).
8. Notifications settings pane (enable switch + 1h/8h/24h snooze + snoozed-until line).
9. This ADR.

## Tests

`test_carestatus` (the transition predicates), `test_careevaluation` (the shared snapshot shape),
`test_format` (the watering-vs-generic phrasing, non-alerting → empty body), `test_settingsstore`
(notification prefs default/persist/clamp/signal), `test_alertcontroller` (the full debounce + gating
matrix on a `FakeClock` with a recording sink). `test_plantlisthealth` proves the `careOf` refactor is
behaviour-preserving. All pure / fake-backed; no judgment logic in QML/JS; the D-Bus backend is the
only untested piece by design (platform glue).

## Out of scope / follow-ups

- Calendar/journal watering reminders (dropped by design — sensor-driven only).
- Per-plant / per-category settings; recovery ("back to ideal") notifications.
- Persisting per-`(plant, quantity)` last-alerted state across restarts.
- Per-platform backends (macOS/Windows/Android/iOS) + a tray icon — arrive with the mobile build.
- A surgical "plants bound to this sensor" lookup to avoid the all-plants re-judge per ingest.
