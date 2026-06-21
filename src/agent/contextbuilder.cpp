// SPDX-License-Identifier: GPL-3.0-or-later
#include "contextbuilder.h"

#include "iplantrepository.h"
#include "plant.h"

#include <algorithm>

namespace klr {

namespace {

QString plural(qint64 n, const QString &unit)
{
    return QString::number(n) + QLatin1Char(' ') + unit + (n == 1 ? QString() : QStringLiteral("s"));
}

} // namespace

ContextBuilder::ContextBuilder(const IPlantRepository &plants)
    : m_plants(plants)
{
}

QString ContextBuilder::build() const
{
    QList<Plant> plants = m_plants.all();
    // all() ordering is unspecified; sort for a byte-stable block (oldest first, id tie-break).
    std::sort(plants.begin(), plants.end(), [](const Plant &a, const Plant &b) {
        if (a.trackedSince != b.trackedSince)
            return a.trackedSince < b.trackedSince;
        return a.id.toString() < b.id.toString();
    });

    if (plants.isEmpty())
        return QStringLiteral("No plants are being tracked yet.");

    QString out = QStringLiteral("Plant roster (%1 tracked):\n").arg(plural(plants.size(), QStringLiteral("plant")));
    for (const Plant &p : plants) {
        const QString name = p.displayName.isEmpty() ? QStringLiteral("(unnamed)") : p.displayName;
        const QString species =
            p.species.isEmpty() ? QStringLiteral("species unknown") : QStringLiteral("species ") + p.species;
        out += QStringLiteral("- %1 (%2)\n").arg(name, species);
    }
    return out;
}

} // namespace klr
