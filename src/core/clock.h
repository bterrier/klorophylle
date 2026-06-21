// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/qglobal.h>

namespace klr {

// Injected time — the single most reusable lesson from WatchFlower
// pure logic takes nowMs() as a dependency,
// so it is deterministic under test. No code reads the wall clock directly.
class Clock {
public:
    virtual ~Clock() = default;
    virtual qint64 nowMs() const = 0;
};

class SystemClock final : public Clock {
public:
    qint64 nowMs() const override;
};

// Test double: time only moves when the test moves it.
class FakeClock final : public Clock {
public:
    qint64 t = 0;
    qint64 nowMs() const override { return t; }
};

} // namespace klr
