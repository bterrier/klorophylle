// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "message.h"

#include <QtCore/QJsonObject>

#include <expected>

namespace karness {

// Canonical JSON codec for Message (docs/adr/0019). The format is
// provider-neutral (tagged blocks, lowercase role strings, strict base64
// for image bytes) and is what fixtures and the transcript persistence
// serialize. The reader is strict: unknown roles/block types and missing
// required fields are errors, not skips.
enum class MessageCodecError {
    NotAnObject,
    UnknownRole,
    UnknownBlockType,
    MissingField,
    InvalidBase64,
};

[[nodiscard]] QJsonObject messageToJson(const Message &message);
[[nodiscard]] std::expected<Message, MessageCodecError> messageFromJson(const QJsonObject &json);

} // namespace karness
