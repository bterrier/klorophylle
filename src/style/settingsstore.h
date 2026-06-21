// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "units.h" // klr::DisplayUnits + the unit-preference enums

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace klr {

class IKeyValueStore;

// The app's declarative preference layer (see docs/adr/0008), exposed to QML as the
// `Settings` singleton. One typed, tested accessor per preference over an injected
// IKeyValueStore — read/write/default can't drift. Like AppContext it uses CONSTRUCTOR
// injection (the only ctor needs the store), so it is not default-constructible and QML
// obtains the composition-root instance via create() (no getInstance, ADR 0002).
//
// Preferences are surfaced as ints (matching QML ComboBox currentIndex) and map 1:1 to
// the klr unit-preference enums. `displayUnits()` is the C++ accessor the view-models
// read to format readings. colorScheme is stored here (the persisted *choice*); Theme
// applies it live (a one-way QML Binding), keeping ThemeController free of persistence.
class SettingsStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON

    Q_PROPERTY(int colorScheme READ colorScheme WRITE setColorScheme NOTIFY colorSchemeChanged)
    Q_PROPERTY(int temperatureUnit READ temperatureUnit WRITE setTemperatureUnit NOTIFY unitsChanged)
    Q_PROPERTY(int illuminanceUnit READ illuminanceUnit WRITE setIlluminanceUnit NOTIFY unitsChanged)
    Q_PROPERTY(int pressureUnit READ pressureUnit WRITE setPressureUnit NOTIFY unitsChanged)
    Q_PROPERTY(int exportPeriodIndex READ exportPeriodIndex WRITE setExportPeriodIndex NOTIFY exportPeriodChanged)
    // History backfill (ADR 0014): whether to auto-download stored history over GATT, and how
    // often at most (each connect drains the sensor battery). Device-local, out of the sync schema.
    Q_PROPERTY(bool historySyncEnabled READ historySyncEnabled WRITE setHistorySyncEnabled NOTIFY historySyncChanged)
    Q_PROPERTY(int historySyncIntervalHours READ historySyncIntervalHours WRITE setHistorySyncIntervalHours NOTIFY historySyncChanged)
    // Care notifications (ADR 0016): a global master switch and a snooze deadline (epoch
    // ms; 0 = not snoozed). Device-local, out of the sync schema. The snooze *deadline* is
    // stored (not a duration) so it survives restart; AppContext computes it from the clock.
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY notificationsChanged)
    Q_PROPERTY(qlonglong notificationsSnoozedUntilMs READ notificationsSnoozedUntilMs WRITE setNotificationsSnoozedUntilMs NOTIFY notificationsChanged)
    // AI assistant (ADR 0019): NON-SECRET provider config — a master switch, the
    // OpenAI-compatible endpoint, and the model name. The API key NEVER lives here (decision 9:
    // never QSettings) — it goes through the ISecretStore seam. Defaults target a local Ollama,
    // which needs no key, so the agent works out of the box once Ollama is running.
    Q_PROPERTY(bool agentEnabled READ agentEnabled WRITE setAgentEnabled NOTIFY agentChanged)
    Q_PROPERTY(QString agentBaseUrl READ agentBaseUrl WRITE setAgentBaseUrl NOTIFY agentChanged)
    Q_PROPERTY(QString agentModel READ agentModel WRITE setAgentModel NOTIFY agentChanged)
    // Whether the assistant is allowed to call the domain tools (list plants, read data/journal,
    // confirmed write). Off lets a small local model that gets overwhelmed by tool definitions
    // still chat plainly. Drives a session rebuild via agentChanged, like the other agent prefs.
    Q_PROPERTY(bool agentToolsEnabled READ agentToolsEnabled WRITE setAgentToolsEnabled NOTIFY agentChanged)
    // Reasoning effort the model is asked to spend (ADR 0019 decision 6). An int matching
    // karness::ReasoningEffort order (0=Off, 1=Low, 2=Medium, 3=High); mapped to the wire per
    // dialect (the compat dialect emits "reasoning_effort"). Device-local; drives a session rebuild.
    Q_PROPERTY(int agentReasoningEffort READ agentReasoningEffort WRITE setAgentReasoningEffort NOTIFY agentChanged)
    // Whether the agent may look plants up online via read_online_plant_db (ADR 0023). OFF by
    // default: a web fetch is network egress to a third party (even with a local LLM), so it is an
    // explicit opt-in. The host curates the allowlist (Wikipedia + Wikispecies). Drives a session
    // rebuild via agentChanged, like the other agent prefs.
    Q_PROPERTY(bool agentWebToolEnabled READ agentWebToolEnabled WRITE setAgentWebToolEnabled NOTIFY agentChanged)
    // Whether the agent may read journal photos and send them to the model (ADR 0025). OFF by
    // default: only works with a vision-capable model, and a photo is sensitive data leaving the
    // device. Drives ModelCaps.vision, the read_plant_photo registration, and a session rebuild.
    Q_PROPERTY(bool agentVisionEnabled READ agentVisionEnabled WRITE setAgentVisionEnabled NOTIFY agentChanged)
    // Which provider dialect to talk (ADR 0019 decision 3). An int selecting the
    // AgentViewModel provider factory's branch: 0=OpenAI-compatible (Chat Completions),
    // 1=OpenAI Responses, 2=Anthropic Messages, 3=Gemini. Device-local; drives a session rebuild.
    Q_PROPERTY(int agentProviderType READ agentProviderType WRITE setAgentProviderType NOTIFY agentChanged)

public:
    explicit SettingsStore(IKeyValueStore *store, QObject *parent = nullptr);

    // Composition root sets this to the single instance before QML loads; create()
    // hands it to the engine (see AppContext for the same pattern).
    static SettingsStore *s_instance;
    static SettingsStore *create(QQmlEngine *, QJSEngine *);

    int colorScheme() const { return m_colorScheme; }
    void setColorScheme(int value);

    int temperatureUnit() const { return static_cast<int>(m_units.temperature); }
    void setTemperatureUnit(int value);
    int illuminanceUnit() const { return static_cast<int>(m_units.illuminance); }
    void setIlluminanceUnit(int value);
    int pressureUnit() const { return static_cast<int>(m_units.pressure); }
    void setPressureUnit(int value);

    // CSV export window, as an index into AppContext's period table (0 = "All data").
    int exportPeriodIndex() const { return m_exportPeriodIndex; }
    void setExportPeriodIndex(int value);

    // The current display-unit preferences, for the view-models' formatting boundary.
    DisplayUnits displayUnits() const { return m_units; }

    bool historySyncEnabled() const { return m_historySyncEnabled; }
    void setHistorySyncEnabled(bool value);
    int historySyncIntervalHours() const { return m_historySyncIntervalHours; }
    void setHistorySyncIntervalHours(int value);

    bool notificationsEnabled() const { return m_notificationsEnabled; }
    void setNotificationsEnabled(bool value);
    qint64 notificationsSnoozedUntilMs() const { return m_notificationsSnoozedUntilMs; }
    void setNotificationsSnoozedUntilMs(qint64 value);

    bool agentEnabled() const { return m_agentEnabled; }
    void setAgentEnabled(bool value);
    QString agentBaseUrl() const { return m_agentBaseUrl; }
    void setAgentBaseUrl(const QString &value);
    QString agentModel() const { return m_agentModel; }
    void setAgentModel(const QString &value);
    bool agentToolsEnabled() const { return m_agentToolsEnabled; }
    void setAgentToolsEnabled(bool value);
    int agentReasoningEffort() const { return m_agentReasoningEffort; }
    void setAgentReasoningEffort(int value);
    int agentProviderType() const { return m_agentProviderType; }
    void setAgentProviderType(int value);
    bool agentWebToolEnabled() const { return m_agentWebToolEnabled; }
    void setAgentWebToolEnabled(bool value);

    bool agentVisionEnabled() const { return m_agentVisionEnabled; }
    void setAgentVisionEnabled(bool value);

signals:
    void colorSchemeChanged();
    void unitsChanged();
    void exportPeriodChanged();
    void historySyncChanged();
    void notificationsChanged();
    void agentChanged();

private:
    IKeyValueStore *m_store;
    DisplayUnits m_units;
    int m_colorScheme { 0 }; // 0 = Light (ThemeController::ColorScheme order)
    int m_exportPeriodIndex { 0 }; // 0 = "All data" (AppContext::exportPeriodLabels order)
    bool m_historySyncEnabled { true };
    int m_historySyncIntervalHours { 6 };
    bool m_notificationsEnabled { true };
    qint64 m_notificationsSnoozedUntilMs { 0 };
    bool m_agentEnabled { true };
    QString m_agentBaseUrl;
    QString m_agentModel;
    bool m_agentToolsEnabled { true };
    int m_agentReasoningEffort { 0 }; // 0 = Off (karness::ReasoningEffort order)
    int m_agentProviderType { 0 };    // 0 = OpenAI-compatible (provider-factory branch order)
    bool m_agentWebToolEnabled { false }; // web lookups are opt-in (off by default)
    bool m_agentVisionEnabled { false };  // journal photos to the model are opt-in (off by default)
};

} // namespace klr
