// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "binding.h"
#include "ids.h"
#include "reading.h"

using namespace klr;

// Pure plant<->sensor binding resolution: active-at-time windows and the per-plant
// overlap validation rule. No DB/BLE/QML — see docs/adr/0005-plant-sensor-binding.md.
class TestBinding : public QObject {
    Q_OBJECT

    static PlantSensorBinding edge(const PlantId &p, const SensorId &s, const QDateTime &from,
                                   std::optional<QDateTime> to = std::nullopt,
                                   std::optional<Quantity> role = std::nullopt)
    {
        return { p, s, from, to, role };
    }

private slots:
    void activeWindowIsHalfOpen()
    {
        const PlantId p = PlantId::generate();
        const SensorId s = SensorId::generate();
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const PlantSensorBinding b = edge(p, s, t0, t0.addDays(10));

        QVERIFY(!isActiveAt(b, t0.addSecs(-1))); // before validFrom
        QVERIFY(isActiveAt(b, t0));              // closed at the bottom
        QVERIFY(isActiveAt(b, t0.addDays(5)));   // inside
        QVERIFY(!isActiveAt(b, t0.addDays(10))); // half-open at the top
        QVERIFY(!isActiveAt(b, t0.addDays(11)));
    }

    void openBindingHasNoUpperBound()
    {
        const PlantSensorBinding b =
            edge(PlantId::generate(), SensorId::generate(),
                 QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC));
        QVERIFY(isActiveAt(b, QDateTime(QDate(2099, 1, 1), QTime(0, 0), QTimeZone::UTC)));
    }

    void instantaneousSwapAttributesToExactlyOneBinding()
    {
        // A swap closes one edge at T and opens the next at T. A sample exactly at T
        // must belong to exactly one binding — the freshly-opened one (half-open top).
        const PlantId p = PlantId::generate();
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const QDateTime swap = t0.addDays(30);
        const PlantSensorBinding all[] = {
            edge(p, SensorId::generate(), t0, swap),
            edge(p, SensorId::generate(), swap),
        };
        const QList<PlantSensorBinding> active = activeBindings(all, swap);
        QCOMPARE(active.size(), 1);
        QCOMPARE(active.first().validFrom, swap);
    }

    void multipleConcurrentBindingsAreAllActive()
    {
        // Two probes in one pot: both active at once (plant -> N sensors).
        const PlantId p = PlantId::generate();
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const PlantSensorBinding all[] = {
            edge(p, SensorId::generate(), t0),
            edge(p, SensorId::generate(), t0.addDays(1)),
        };
        QCOMPARE(activeBindings(all, t0.addDays(2)).size(), 2);
    }

    void noRoleBindingsNeverConflict()
    {
        // Redundant no-role bindings for the same quantity are allowed.
        const PlantId p = PlantId::generate();
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const PlantSensorBinding existing[] = { edge(p, SensorId::generate(), t0) };
        const PlantSensorBinding candidate = edge(p, SensorId::generate(), t0);
        QVERIFY(validateBinding(existing, candidate).has_value());
    }

    void overlappingExplicitRoleForSameQuantityIsRejected()
    {
        const PlantId p = PlantId::generate();
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const PlantSensorBinding existing[] = {
            edge(p, SensorId::generate(), t0, std::nullopt, Quantity::AirTemperature),
        };
        const PlantSensorBinding candidate =
            edge(p, SensorId::generate(), t0.addDays(1), std::nullopt, Quantity::AirTemperature);
        const auto r = validateBinding(existing, candidate);
        QVERIFY(!r.has_value());
        QCOMPARE(r.error(), BindingError::RoleConflict);
    }

    void explicitRoleForDifferentQuantityIsAllowed()
    {
        const PlantId p = PlantId::generate();
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const PlantSensorBinding existing[] = {
            edge(p, SensorId::generate(), t0, std::nullopt, Quantity::AirTemperature),
        };
        const PlantSensorBinding candidate =
            edge(p, SensorId::generate(), t0, std::nullopt, Quantity::SoilMoisture);
        QVERIFY(validateBinding(existing, candidate).has_value());
    }

    void nonOverlappingExplicitRoleForSameQuantityIsAllowed()
    {
        // A swap: the old role binding is closed before the new one opens.
        const PlantId p = PlantId::generate();
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const PlantSensorBinding existing[] = {
            edge(p, SensorId::generate(), t0, t0.addDays(10), Quantity::AirTemperature),
        };
        const PlantSensorBinding candidate =
            edge(p, SensorId::generate(), t0.addDays(10), std::nullopt, Quantity::AirTemperature);
        QVERIFY(validateBinding(existing, candidate).has_value());
    }

    void noRolePlusExplicitRoleOverlapIsAllowed()
    {
        const PlantId p = PlantId::generate();
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const PlantSensorBinding existing[] = {
            edge(p, SensorId::generate(), t0), // no role
        };
        const PlantSensorBinding candidate =
            edge(p, SensorId::generate(), t0, std::nullopt, Quantity::AirTemperature);
        QVERIFY(validateBinding(existing, candidate).has_value());
    }
};

QTEST_APPLESS_MAIN(TestBinding)
#include "test_binding.moc"
