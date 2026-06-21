// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "inmemorykeyvaluestore.h"
#include "settingsstore.h"
#include "units.h"

using namespace klr;

// SettingsStore behaviour over the in-memory key/value fake: defaults, persistence,
// change signals, round-trip on a fresh store, and the displayUnits() accessor the
// view-models read. No QML engine, no QSettings file.
class TestSettingsStore : public QObject {
    Q_OBJECT

private slots:
    void defaultsAreCanonical()
    {
        InMemoryKeyValueStore kv;
        SettingsStore s(&kv);
        QCOMPARE(s.colorScheme(), 0);
        QCOMPARE(s.temperatureUnit(), int(TemperatureUnit::Celsius));
        QCOMPARE(s.illuminanceUnit(), int(IlluminanceUnit::Lux));
        QCOMPARE(s.pressureUnit(), int(PressureUnit::Hectopascal));

        const DisplayUnits u = s.displayUnits();
        QCOMPARE(u.temperature, TemperatureUnit::Celsius);
        QCOMPARE(u.illuminance, IlluminanceUnit::Lux);
        QCOMPARE(u.pressure, PressureUnit::Hectopascal);
    }

    void settersEmitAndPersist()
    {
        InMemoryKeyValueStore kv;
        SettingsStore s(&kv);
        QSignalSpy units(&s, &SettingsStore::unitsChanged);
        QSignalSpy scheme(&s, &SettingsStore::colorSchemeChanged);

        s.setTemperatureUnit(int(TemperatureUnit::Fahrenheit));
        s.setIlluminanceUnit(int(IlluminanceUnit::Micromole));
        s.setPressureUnit(int(PressureUnit::MmHg));
        s.setColorScheme(2); // Auto

        QCOMPARE(units.count(), 3);
        QCOMPARE(scheme.count(), 1);
        QCOMPARE(s.displayUnits().temperature, TemperatureUnit::Fahrenheit);
        QCOMPARE(s.displayUnits().pressure, PressureUnit::MmHg);
    }

    void noSignalWhenUnchanged()
    {
        InMemoryKeyValueStore kv;
        SettingsStore s(&kv);
        QSignalSpy units(&s, &SettingsStore::unitsChanged);
        s.setTemperatureUnit(int(TemperatureUnit::Celsius)); // already the default
        QCOMPARE(units.count(), 0);
    }

    void roundTripsAcrossInstances()
    {
        InMemoryKeyValueStore kv;
        {
            SettingsStore s(&kv);
            s.setTemperatureUnit(int(TemperatureUnit::Fahrenheit));
            s.setColorScheme(1); // Dark
        }
        // A fresh store over the same backing reads the persisted choices.
        SettingsStore reopened(&kv);
        QCOMPARE(reopened.temperatureUnit(), int(TemperatureUnit::Fahrenheit));
        QCOMPARE(reopened.colorScheme(), 1);
    }

    void outOfRangeIsClamped()
    {
        InMemoryKeyValueStore kv;
        SettingsStore s(&kv);
        s.setColorScheme(99);          // invalid -> clamped to 0
        QCOMPARE(s.colorScheme(), 0);
        s.setTemperatureUnit(-3);      // invalid -> clamped to 0 (no-op from default)
        QCOMPARE(s.temperatureUnit(), int(TemperatureUnit::Celsius));
    }

    void exportPeriodDefaultsPersistsAndClamps()
    {
        InMemoryKeyValueStore kv;
        {
            SettingsStore s(&kv);
            QCOMPARE(s.exportPeriodIndex(), 0); // default = "All data"

            QSignalSpy spy(&s, &SettingsStore::exportPeriodChanged);
            s.setExportPeriodIndex(2); // "Last 7 days"
            QCOMPARE(s.exportPeriodIndex(), 2);
            QCOMPARE(spy.count(), 1);

            s.setExportPeriodIndex(2); // unchanged -> no signal
            QCOMPARE(spy.count(), 1);

            s.setExportPeriodIndex(99); // out of range -> clamped to 0
            QCOMPARE(s.exportPeriodIndex(), 0);
        }
        // Persisted across instances.
        {
            SettingsStore s(&kv);
            s.setExportPeriodIndex(3); // "Last 30 days"
        }
        SettingsStore reopened(&kv);
        QCOMPARE(reopened.exportPeriodIndex(), 3);
    }

    void notificationPrefsDefaultPersistAndSignal()
    {
        InMemoryKeyValueStore kv;
        {
            SettingsStore s(&kv);
            QCOMPARE(s.notificationsEnabled(), true);       // on by default
            QCOMPARE(s.notificationsSnoozedUntilMs(), qint64(0)); // not snoozed

            QSignalSpy spy(&s, &SettingsStore::notificationsChanged);
            s.setNotificationsEnabled(false);
            QCOMPARE(s.notificationsEnabled(), false);
            QCOMPARE(spy.count(), 1);
            s.setNotificationsEnabled(false); // unchanged -> no signal
            QCOMPARE(spy.count(), 1);

            s.setNotificationsSnoozedUntilMs(1'700'000'000'000);
            QCOMPARE(s.notificationsSnoozedUntilMs(), qint64(1'700'000'000'000));
            QCOMPARE(spy.count(), 2);

            s.setNotificationsSnoozedUntilMs(-5); // a negative deadline is clamped to 0 (not snoozed)
            QCOMPARE(s.notificationsSnoozedUntilMs(), qint64(0));
        }
        // Persisted across instances.
        SettingsStore reopened(&kv);
        QCOMPARE(reopened.notificationsEnabled(), false);
        QCOMPARE(reopened.notificationsSnoozedUntilMs(), qint64(0));
    }

    void agentConfigDefaultsPersistAndSignal()
    {
        InMemoryKeyValueStore kv;
        {
            SettingsStore s(&kv);
            QCOMPARE(s.agentEnabled(), true);                                   // on by default
            QCOMPARE(s.agentBaseUrl(), QStringLiteral("http://localhost:11434/v1")); // local Ollama
            QCOMPARE(s.agentModel(), QStringLiteral("qwen2.5"));

            QSignalSpy spy(&s, &SettingsStore::agentChanged);
            s.setAgentBaseUrl(QStringLiteral("https://api.example.com/v1"));
            s.setAgentModel(QStringLiteral("gpt-4o-mini"));
            s.setAgentEnabled(false);
            QCOMPARE(spy.count(), 3);
            s.setAgentModel(QStringLiteral("gpt-4o-mini")); // unchanged -> no signal
            QCOMPARE(spy.count(), 3);
        }
        // Persisted across instances.
        SettingsStore reopened(&kv);
        QCOMPARE(reopened.agentEnabled(), false);
        QCOMPARE(reopened.agentBaseUrl(), QStringLiteral("https://api.example.com/v1"));
        QCOMPARE(reopened.agentModel(), QStringLiteral("gpt-4o-mini"));
    }

    void agentReasoningEffortDefaultsPersistsAndClamps()
    {
        InMemoryKeyValueStore kv;
        {
            SettingsStore s(&kv);
            QCOMPARE(s.agentReasoningEffort(), 0); // Off by default

            QSignalSpy spy(&s, &SettingsStore::agentChanged);
            s.setAgentReasoningEffort(2); // Medium
            QCOMPARE(s.agentReasoningEffort(), 2);
            QCOMPARE(spy.count(), 1);
            s.setAgentReasoningEffort(2); // unchanged -> no signal
            QCOMPARE(spy.count(), 1);
            s.setAgentReasoningEffort(99); // out of range -> clamped to 0
            QCOMPARE(s.agentReasoningEffort(), 0);
        }
        // Persisted across instances.
        {
            SettingsStore s(&kv);
            s.setAgentReasoningEffort(3); // High
        }
        SettingsStore reopened(&kv);
        QCOMPARE(reopened.agentReasoningEffort(), 3);
    }

    void agentProviderTypeDefaultsPersistsAndClamps()
    {
        InMemoryKeyValueStore kv;
        {
            SettingsStore s(&kv);
            QCOMPARE(s.agentProviderType(), 0); // OpenAI-compatible by default

            QSignalSpy spy(&s, &SettingsStore::agentChanged);
            s.setAgentProviderType(2); // Anthropic
            QCOMPARE(s.agentProviderType(), 2);
            QCOMPARE(spy.count(), 1);
            s.setAgentProviderType(2); // unchanged -> no signal
            QCOMPARE(spy.count(), 1);
            s.setAgentProviderType(99); // out of range -> clamped to 0
            QCOMPARE(s.agentProviderType(), 0);
        }
        // Persisted across instances.
        {
            SettingsStore s(&kv);
            s.setAgentProviderType(3); // Gemini
        }
        SettingsStore reopened(&kv);
        QCOMPARE(reopened.agentProviderType(), 3);
    }

    void agentWebToolDefaultsOffPersistsAndSignals()
    {
        InMemoryKeyValueStore kv;
        {
            SettingsStore s(&kv);
            QCOMPARE(s.agentWebToolEnabled(), false); // opt-in: OFF by default

            QSignalSpy spy(&s, &SettingsStore::agentChanged);
            s.setAgentWebToolEnabled(true);
            QCOMPARE(s.agentWebToolEnabled(), true);
            QCOMPARE(spy.count(), 1);
            s.setAgentWebToolEnabled(true); // unchanged -> no signal
            QCOMPARE(spy.count(), 1);
        }
        // Persisted across instances.
        SettingsStore reopened(&kv);
        QCOMPARE(reopened.agentWebToolEnabled(), true);
    }

    void agentVisionDefaultsOffPersistsAndSignals()
    {
        InMemoryKeyValueStore kv;
        {
            SettingsStore s(&kv);
            QCOMPARE(s.agentVisionEnabled(), false); // opt-in: OFF by default

            QSignalSpy spy(&s, &SettingsStore::agentChanged);
            s.setAgentVisionEnabled(true);
            QCOMPARE(s.agentVisionEnabled(), true);
            QCOMPARE(spy.count(), 1);
            s.setAgentVisionEnabled(true); // unchanged -> no signal
            QCOMPARE(spy.count(), 1);
        }
        SettingsStore reopened(&kv);
        QCOMPARE(reopened.agentVisionEnabled(), true);
    }
};

QTEST_GUILESS_MAIN(TestSettingsStore)
#include "test_settingsstore.moc"
