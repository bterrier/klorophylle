// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <QtCore/QString>
#include <QtCore/QDateTime>

// A Plant is the first-class citizen (plant-first, goal #1): it exists with NO
// sensor at all, and can be journalled and watered on its own. Sensors get bound
// to it later; the binding is what makes a reading history follow the plant.
//
// A plain value type (copied by value), like Reading — no QObject identity is
// needed until a view-model exposes it to QML.
namespace klr {

struct Plant {
    PlantId id;
    QString displayName;          // user-chosen
    QString species;              // freeform for now; SpeciesId/catalog lookup comes later
    QDateTime trackedSince;       // when the user started tracking this plant (UTC)

    bool operator==(const Plant &) const = default;
};

} // namespace klr
