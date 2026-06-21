// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"

#include <expected>

// Deletes a registered sensor and all of its stored data (data hygiene). Only a sensor
// NO plant references — neither an open NOR a closed binding — may be deleted: a closed
// binding still ties the sensor's readings to that plant's history (history follows the
// plant through the binding window, ADR 0005), and detaching only closes the binding, it
// does not remove it. Bindings vanish only when their plant is deleted (FK cascade), so a
// deletable sensor is a true orphan (e.g. left behind when its last plant was removed).
//
// Pure orchestration over the repository interfaces (no SQL of its own — respects the
// repository boundary), so it is unit-tested against the in-memory fakes. The SQLite
// schema would cascade the sensor's readings + bindings on the sensor delete anyway;
// clearing them explicitly here keeps one code path that converges with the in-memory
// fakes (whose repos are independent objects). Not one atomic unit — each repository
// commits its own transaction — but a delete is monotonic (nothing to compensate).
namespace klr {

class ISensorRepository;
class IBindingRepository;
class IReadingRepository;

enum class SensorDeleteError {
    NotFound,   // no sensor with this id
    StillBound, // a plant still references it (open or closed binding) — its data is part
                // of that plant's history; deletable only once no plant uses it
};

class SensorDeleter {
public:
    SensorDeleter(ISensorRepository &sensors, IBindingRepository &bindings,
                  IReadingRepository &readings);

    // Delete `sensor`, its readings, and its (closed) bindings. Returns the error when
    // the sensor is unknown or still bound to a plant; the data is then left untouched.
    std::expected<void, SensorDeleteError> remove(SensorId sensor);

private:
    ISensorRepository &m_sensors;
    IBindingRepository &m_bindings;
    IReadingRepository &m_readings;
};

} // namespace klr
