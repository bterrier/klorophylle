// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>

#include <optional>

namespace karness {

// The runtime error vocabulary of the streaming/provider path (docs/adr/0019).
// Codec-level failures use their own small enums (see messagecodec.h); dialects
// map provider-side parse failures to Code::Parse at the streaming layer.
struct AgentError {
    enum class Code {
        Network,    // transport failure (DNS, TLS, connection reset)
        Http,       // non-2xx response; httpStatus carries the code
        Provider,   // provider-reported error payload
        Parse,      // unparseable stream/tool-call payload
        Cancelled,  // user/host cancelled the turn
        Timeout,    // per-turn timeout elapsed
        LoopLimit,  // agent loop hit its bounded-iteration guard
        NotReady,   // provider has no usable model/endpoint
    };

    Code code = Code::Provider;
    QString message;
    std::optional<int> httpStatus;

    bool operator==(const AgentError &) const = default;
};

} // namespace karness
