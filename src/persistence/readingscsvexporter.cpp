// SPDX-License-Identifier: GPL-3.0-or-later
#include "readingscsvexporter.h"

#include "backuptokens.h"
#include "binding.h"
#include "format.h" // unitSymbol
#include "ibindingrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "plant.h"

#include <QtCore/QTimeZone>

namespace klr {

namespace {

// The model of the sensor that supplied `q` to this plant at `ts` — best effort for a
// human CSV: among the bindings active at `ts` that can supply q (matching role, or
// no-role = supplies everything), prefer the explicit-role one, mirroring the
// aggregation precedence in seriesForPlant.
QString sensorModelAt(const ISensorRepository &sensors,
                      const QList<PlantSensorBinding> &bindings, Quantity q,
                      const QDateTime &ts)
{
    const PlantSensorBinding *chosen = nullptr;
    for (const PlantSensorBinding &b : bindings) {
        if (!isActiveAt(b, ts))
            continue;
        if (b.role.has_value() && *b.role != q)
            continue; // role-restricted to another quantity
        if (b.role.has_value()) {
            chosen = &b; // explicit role wins outright
            break;
        }
        if (!chosen)
            chosen = &b; // first no-role candidate, keep looking for a role match
    }
    if (!chosen)
        return {};
    const std::optional<Sensor> s = sensors.get(chosen->sensor);
    return s ? s->model : QString();
}

} // namespace

QString csvField(const QString &field)
{
    if (!field.contains(QLatin1Char(',')) && !field.contains(QLatin1Char('"'))
        && !field.contains(QLatin1Char('\n')) && !field.contains(QLatin1Char('\r')))
        return field;
    QString escaped = field;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

ReadingsCsvExporter::ReadingsCsvExporter(IPlantRepository &plants, IBindingRepository &bindings,
                                         IReadingRepository &readings, ISensorRepository &sensors)
    : m_plants(plants), m_bindings(bindings), m_readings(readings), m_sensors(sensors)
{
}

QString ReadingsCsvExporter::exportCsv(const DisplayUnits &units, const QDateTime &from,
                                       const QDateTime &to) const
{
    QString out = QStringLiteral("plant,species,sensor_model,timestamp,quantity,value,unit,provenance\n");

    for (const Plant &plant : m_plants.all()) {
        const QList<PlantSensorBinding> binds = m_bindings.bindings(plant.id);
        if (binds.isEmpty())
            continue; // no sensor ever bound — no reading history to dump

        const QString plantField = csvField(plant.displayName);
        const QString speciesField = csvField(plant.species);

        for (int qi = 0; qi < kQuantityCount; ++qi) {
            const auto q = static_cast<Quantity>(qi);
            const QList<Reading> series = m_readings.seriesForPlant(binds, q, from, to);
            if (series.isEmpty())
                continue;

            const Unit du = displayUnit(q, units);
            const QString unitField = csvField(unitSymbol(du));
            const QString quantityField = backuptokens::toToken(q);

            for (const Reading &r : series) {
                QString valueField;
                if (r.value.has_value())
                    valueField = QString::number(convert(*r.value, canonicalUnit(q), du), 'g', 6);

                out += plantField;
                out += QLatin1Char(',');
                out += speciesField;
                out += QLatin1Char(',');
                out += csvField(sensorModelAt(m_sensors, binds, q, r.timestamp));
                out += QLatin1Char(',');
                out += r.timestamp.toLocalTime().toString(Qt::ISODate);
                out += QLatin1Char(',');
                out += quantityField;
                out += QLatin1Char(',');
                out += valueField;
                out += QLatin1Char(',');
                out += unitField;
                out += QLatin1Char(',');
                out += backuptokens::toToken(r.provenance);
                out += QLatin1Char('\n');
            }
        }
    }
    return out;
}

} // namespace klr
