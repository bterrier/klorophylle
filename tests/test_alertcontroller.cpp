// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "alertcontroller.h"
#include "carestatus.h"
#include "clock.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemorykeyvaluestore.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "inotificationsink.h"
#include "plant.h"
#include "settingsstore.h"

#include <array>

using namespace klr;

namespace {
// Records every delivered notification so a test can assert count + content.
class RecordingSink final : public INotificationSink {
public:
    struct Note { QString title; QString body; };
    QList<Note> notes;
    void notify(const QString &title, const QString &body) override { notes.append({ title, body }); }
};
} // namespace

// The transition debounce (ADR 0016): a notification fires once when a plant crosses INTO
// an out-of-range state, not once per advertisement, and global-disable / snooze gate delivery
// while still priming the memory (so un-muting never replays a skipped transition). FakeClock +
// in-memory repos; no QML, no DBus (delivery goes to a recording fake).
class TestAlertController : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    // Build a plant bound to one sensor with a soil-moisture range [30,60].
    struct Fixture {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        InMemorySensorRepository sensors;
        InMemoryKeyValueStore kv;
        SettingsStore settings;
        PlantId plant;
        SensorId sensor;
        explicit Fixture(FakeClock &clock) : sensors(clock), settings(&kv)
        {
            const QDateTime now = QDateTime::fromMSecsSinceEpoch(clock.nowMs(), QTimeZone::UTC);
            Plant p;
            p.id = PlantId::generate();
            p.displayName = QStringLiteral("Basil");
            p.trackedSince = now;
            plants.add(p);
            plant = p.id;
            sensor = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
            bindings.bind(plant, sensor, now, std::nullopt);
            thresholds.setRange(plant, CareRange{ Quantity::SoilMoisture, 30.0, 60.0 });
        }
    };

    // Append a moisture sample at the current clock time.
    void putMoisture(Fixture &f, double pct)
    {
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        f.readings.append(f.sensor, std::array{ Reading{ Quantity::SoilMoisture, pct, Unit::Percent,
                                                         now, Provenance::Advertisement } });
    }

private slots:
    void init() { m_clock.t = QDateTime(QDate(2026, 3, 1), QTime(9, 0), QTimeZone::UTC)
                                  .toMSecsSinceEpoch(); }

    void firesOncePerTransitionNotPerSample()
    {
        Fixture f(m_clock);
        RecordingSink sink;
        AlertController alerts(f.plants, f.bindings, f.readings, f.thresholds, nullptr, m_clock,
                               f.settings, sink);

        putMoisture(f, 10.0); // dry — crosses INTO TooLow
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 1);
        QVERIFY(sink.notes.first().title.contains(QStringLiteral("Basil")));
        QVERIFY(sink.notes.first().body.contains(QStringLiteral("water"), Qt::CaseInsensitive));

        // Still dry across more advertisements → no new notification (debounced).
        m_clock.t += 60'000;
        putMoisture(f, 12.0);
        alerts.evaluate();
        m_clock.t += 60'000;
        putMoisture(f, 9.0);
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 1);
    }

    void recoveryIsSilentAndReDropReFires()
    {
        Fixture f(m_clock);
        RecordingSink sink;
        AlertController alerts(f.plants, f.bindings, f.readings, f.thresholds, nullptr, m_clock,
                               f.settings, sink);

        putMoisture(f, 10.0);
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 1);

        m_clock.t += 3'600'000;
        putMoisture(f, 45.0); // back in range — recovery, no notification
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 1);

        m_clock.t += 3'600'000;
        putMoisture(f, 8.0); // dry again — a fresh transition fires
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 2);
    }

    void firstSeenDryFiresAtStartup()
    {
        // A plant already dry when the controller first sees it: Unknown → TooLow is a
        // transition, so the user is told on the first advertisement ("needs water now").
        Fixture f(m_clock);
        RecordingSink sink;
        AlertController alerts(f.plants, f.bindings, f.readings, f.thresholds, nullptr, m_clock,
                               f.settings, sink);
        putMoisture(f, 5.0);
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 1);
    }

    void globalDisableSuppressesButPrimes()
    {
        Fixture f(m_clock);
        RecordingSink sink;
        AlertController alerts(f.plants, f.bindings, f.readings, f.thresholds, nullptr, m_clock,
                               f.settings, sink);
        f.settings.setNotificationsEnabled(false);

        putMoisture(f, 10.0);
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 0); // muted — nothing delivered

        // Re-enable: the plant is STILL dry (no new transition), and the memory was primed
        // while muted, so nothing replays.
        f.settings.setNotificationsEnabled(true);
        m_clock.t += 60'000;
        putMoisture(f, 11.0);
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 0);
    }

    void snoozeSuppressesUntilDeadlineThenNewTransitionsFire()
    {
        Fixture f(m_clock);
        RecordingSink sink;
        AlertController alerts(f.plants, f.bindings, f.readings, f.thresholds, nullptr, m_clock,
                               f.settings, sink);
        f.settings.setNotificationsSnoozedUntilMs(m_clock.nowMs() + 3'600'000); // 1h

        putMoisture(f, 10.0); // a transition during the snooze window — suppressed
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 0);

        // Past the deadline: recover (primes Ideal), then a fresh drop fires.
        m_clock.t += 2 * 3'600'000;
        putMoisture(f, 45.0);
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 0);
        m_clock.t += 60'000;
        putMoisture(f, 9.0);
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 1);
    }

    void noRangeNoNotification()
    {
        // A plant with a reading but no threshold/species → nothing to judge → silent.
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds; // empty
        InMemorySensorRepository sensors(m_clock);
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);
        RecordingSink sink;

        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        Plant p;
        p.id = PlantId::generate();
        p.displayName = QStringLiteral("p");
        p.trackedSince = now;
        plants.add(p);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("BB"), QStringLiteral("m"));
        bindings.bind(p.id, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 5.0, Unit::Percent, now,
                                                 Provenance::Advertisement } });

        AlertController alerts(plants, bindings, readings, thresholds, nullptr, m_clock, settings,
                               sink);
        alerts.evaluate();
        QCOMPARE(sink.notes.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestAlertController)
#include "test_alertcontroller.moc"
