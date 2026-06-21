// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>
#include <stdexcept>

// Storage is fail-loud: a migration step or a mutation that cannot complete throws
// (never silently advances schema_version or drops data — the failure mode
// WatchFlower could hit). Decoders return std::optional; repos that
// hit a genuine SQL failure throw StorageError.
namespace klr {

class StorageError : public std::runtime_error {
public:
    explicit StorageError(const QString &what)
        : std::runtime_error(what.toStdString()) {}
};

} // namespace klr
