// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace karness {

// Declaration of one callable tool, advertised to the model. inputSchema is
// a JSON Schema object; dialects translate it to each provider's tool format.
struct ToolSpec {
    QString name;
    QString description;
    QJsonObject inputSchema;

    bool operator==(const ToolSpec &) const = default;
};

} // namespace karness
