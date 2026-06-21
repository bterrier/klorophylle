// SPDX-License-Identifier: GPL-3.0-or-later
#include "settingsstore.h"

#include "ikeyvaluestore.h"

#include <QtQml/QQmlEngine>

#include <algorithm>

namespace klr {

namespace {
constexpr auto kColorSchemeKey = "appearance/colorScheme";
constexpr auto kTemperatureKey = "units/temperature";
constexpr auto kIlluminanceKey = "units/illuminance";
constexpr auto kPressureKey = "units/pressure";
constexpr auto kExportPeriodKey = "export/periodIndex";
constexpr auto kHistorySyncEnabledKey = "history/syncEnabled";
constexpr auto kHistorySyncIntervalKey = "history/syncIntervalHours";
constexpr auto kNotificationsEnabledKey = "notifications/enabled";
constexpr auto kNotificationsSnoozedUntilKey = "notifications/snoozedUntilMs";
constexpr auto kAgentEnabledKey = "agent/enabled";
constexpr auto kAgentBaseUrlKey = "agent/baseUrl";
constexpr auto kAgentModelKey = "agent/model";
constexpr auto kAgentToolsEnabledKey = "agent/toolsEnabled";
constexpr auto kAgentReasoningEffortKey = "agent/reasoningEffort";
constexpr auto kAgentProviderTypeKey = "agent/providerType";
constexpr auto kAgentWebToolEnabledKey = "agent/webToolEnabled";
constexpr auto kAgentVisionEnabledKey = "agent/visionEnabled";

// Number of reasoning-effort levels — must match karness::ReasoningEffort (Off/Low/Medium/High).
constexpr int kReasoningEffortCount = 4;

// Number of provider dialects — must match the AgentViewModel provider factory's branches
// (OpenAI-compatible / OpenAI Responses / Anthropic / Gemini).
constexpr int kProviderTypeCount = 4;

// Defaults target a local Ollama (OpenAI-compatible at /v1) with a common tool-capable model.
constexpr auto kAgentBaseUrlDefault = "http://localhost:11434/v1";
constexpr auto kAgentModelDefault = "qwen2.5";

// Number of CSV export periods — must match AppContext::kExportPeriods.
constexpr int kExportPeriodCount = 5;

// Bounds for the history-sync cadence (hours): at least hourly, at most weekly.
constexpr int kHistorySyncIntervalMin = 1;
constexpr int kHistorySyncIntervalMax = 168;

// Clamp a persisted/QML int into [0, count) so a stale or out-of-range value can never
// select a bogus enum.
int clampIndex(int value, int count)
{
    return (value < 0 || value >= count) ? 0 : value;
}
} // namespace

SettingsStore *SettingsStore::s_instance = nullptr;

SettingsStore::SettingsStore(IKeyValueStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    m_colorScheme = clampIndex(m_store->value(QString::fromLatin1(kColorSchemeKey), 0).toInt(), 3);
    m_units.temperature = static_cast<TemperatureUnit>(
        clampIndex(m_store->value(QString::fromLatin1(kTemperatureKey), 0).toInt(), 2));
    m_units.illuminance = static_cast<IlluminanceUnit>(
        clampIndex(m_store->value(QString::fromLatin1(kIlluminanceKey), 0).toInt(), 2));
    m_units.pressure = static_cast<PressureUnit>(
        clampIndex(m_store->value(QString::fromLatin1(kPressureKey), 0).toInt(), 3));
    m_exportPeriodIndex =
        clampIndex(m_store->value(QString::fromLatin1(kExportPeriodKey), 0).toInt(), kExportPeriodCount);
    m_historySyncEnabled =
        m_store->value(QString::fromLatin1(kHistorySyncEnabledKey), true).toBool();
    m_historySyncIntervalHours = std::clamp(
        m_store->value(QString::fromLatin1(kHistorySyncIntervalKey), 6).toInt(),
        kHistorySyncIntervalMin, kHistorySyncIntervalMax);
    m_notificationsEnabled =
        m_store->value(QString::fromLatin1(kNotificationsEnabledKey), true).toBool();
    m_notificationsSnoozedUntilMs = std::max<qint64>(
        0, m_store->value(QString::fromLatin1(kNotificationsSnoozedUntilKey), 0).toLongLong());
    m_agentEnabled = m_store->value(QString::fromLatin1(kAgentEnabledKey), true).toBool();
    m_agentBaseUrl = m_store->value(QString::fromLatin1(kAgentBaseUrlKey),
                                    QString::fromLatin1(kAgentBaseUrlDefault)).toString();
    m_agentModel = m_store->value(QString::fromLatin1(kAgentModelKey),
                                  QString::fromLatin1(kAgentModelDefault)).toString();
    m_agentToolsEnabled = m_store->value(QString::fromLatin1(kAgentToolsEnabledKey), true).toBool();
    m_agentReasoningEffort = clampIndex(
        m_store->value(QString::fromLatin1(kAgentReasoningEffortKey), 0).toInt(), kReasoningEffortCount);
    m_agentProviderType = clampIndex(
        m_store->value(QString::fromLatin1(kAgentProviderTypeKey), 0).toInt(), kProviderTypeCount);
    m_agentWebToolEnabled =
        m_store->value(QString::fromLatin1(kAgentWebToolEnabledKey), false).toBool();
    m_agentVisionEnabled =
        m_store->value(QString::fromLatin1(kAgentVisionEnabledKey), false).toBool();
}

SettingsStore *SettingsStore::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "SettingsStore::create",
               "composition root must set SettingsStore::s_instance before loading QML");
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

void SettingsStore::setColorScheme(int value)
{
    const int v = clampIndex(value, 3);
    if (v == m_colorScheme)
        return;
    m_colorScheme = v;
    m_store->setValue(QString::fromLatin1(kColorSchemeKey), v);
    emit colorSchemeChanged();
}

void SettingsStore::setTemperatureUnit(int value)
{
    const auto u = static_cast<TemperatureUnit>(clampIndex(value, 2));
    if (u == m_units.temperature)
        return;
    m_units.temperature = u;
    m_store->setValue(QString::fromLatin1(kTemperatureKey), static_cast<int>(u));
    emit unitsChanged();
}

void SettingsStore::setIlluminanceUnit(int value)
{
    const auto u = static_cast<IlluminanceUnit>(clampIndex(value, 2));
    if (u == m_units.illuminance)
        return;
    m_units.illuminance = u;
    m_store->setValue(QString::fromLatin1(kIlluminanceKey), static_cast<int>(u));
    emit unitsChanged();
}

void SettingsStore::setPressureUnit(int value)
{
    const auto u = static_cast<PressureUnit>(clampIndex(value, 3));
    if (u == m_units.pressure)
        return;
    m_units.pressure = u;
    m_store->setValue(QString::fromLatin1(kPressureKey), static_cast<int>(u));
    emit unitsChanged();
}

void SettingsStore::setExportPeriodIndex(int value)
{
    const int v = clampIndex(value, kExportPeriodCount);
    if (v == m_exportPeriodIndex)
        return;
    m_exportPeriodIndex = v;
    m_store->setValue(QString::fromLatin1(kExportPeriodKey), v);
    emit exportPeriodChanged();
}

void SettingsStore::setHistorySyncEnabled(bool value)
{
    if (value == m_historySyncEnabled)
        return;
    m_historySyncEnabled = value;
    m_store->setValue(QString::fromLatin1(kHistorySyncEnabledKey), value);
    emit historySyncChanged();
}

void SettingsStore::setHistorySyncIntervalHours(int value)
{
    const int v = std::clamp(value, kHistorySyncIntervalMin, kHistorySyncIntervalMax);
    if (v == m_historySyncIntervalHours)
        return;
    m_historySyncIntervalHours = v;
    m_store->setValue(QString::fromLatin1(kHistorySyncIntervalKey), v);
    emit historySyncChanged();
}

void SettingsStore::setNotificationsEnabled(bool value)
{
    if (value == m_notificationsEnabled)
        return;
    m_notificationsEnabled = value;
    m_store->setValue(QString::fromLatin1(kNotificationsEnabledKey), value);
    emit notificationsChanged();
}

void SettingsStore::setNotificationsSnoozedUntilMs(qint64 value)
{
    const qint64 v = std::max<qint64>(0, value); // a negative deadline is never snoozed
    if (v == m_notificationsSnoozedUntilMs)
        return;
    m_notificationsSnoozedUntilMs = v;
    m_store->setValue(QString::fromLatin1(kNotificationsSnoozedUntilKey), v);
    emit notificationsChanged();
}

void SettingsStore::setAgentEnabled(bool value)
{
    if (value == m_agentEnabled)
        return;
    m_agentEnabled = value;
    m_store->setValue(QString::fromLatin1(kAgentEnabledKey), value);
    emit agentChanged();
}

void SettingsStore::setAgentBaseUrl(const QString &value)
{
    if (value == m_agentBaseUrl)
        return;
    m_agentBaseUrl = value;
    m_store->setValue(QString::fromLatin1(kAgentBaseUrlKey), value);
    emit agentChanged();
}

void SettingsStore::setAgentModel(const QString &value)
{
    if (value == m_agentModel)
        return;
    m_agentModel = value;
    m_store->setValue(QString::fromLatin1(kAgentModelKey), value);
    emit agentChanged();
}

void SettingsStore::setAgentToolsEnabled(bool value)
{
    if (value == m_agentToolsEnabled)
        return;
    m_agentToolsEnabled = value;
    m_store->setValue(QString::fromLatin1(kAgentToolsEnabledKey), value);
    emit agentChanged();
}

void SettingsStore::setAgentReasoningEffort(int value)
{
    const int v = clampIndex(value, kReasoningEffortCount);
    if (v == m_agentReasoningEffort)
        return;
    m_agentReasoningEffort = v;
    m_store->setValue(QString::fromLatin1(kAgentReasoningEffortKey), v);
    emit agentChanged();
}

void SettingsStore::setAgentProviderType(int value)
{
    const int v = clampIndex(value, kProviderTypeCount);
    if (v == m_agentProviderType)
        return;
    m_agentProviderType = v;
    m_store->setValue(QString::fromLatin1(kAgentProviderTypeKey), v);
    emit agentChanged();
}

void SettingsStore::setAgentWebToolEnabled(bool value)
{
    if (value == m_agentWebToolEnabled)
        return;
    m_agentWebToolEnabled = value;
    m_store->setValue(QString::fromLatin1(kAgentWebToolEnabledKey), value);
    emit agentChanged();
}

void SettingsStore::setAgentVisionEnabled(bool value)
{
    if (value == m_agentVisionEnabled)
        return;
    m_agentVisionEnabled = value;
    m_store->setValue(QString::fromLatin1(kAgentVisionEnabledKey), value);
    emit agentChanged();
}

} // namespace klr
