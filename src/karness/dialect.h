// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "agenterror.h"
#include "decodedchunk.h"
#include "inferencerequest.h"
#include "sseparser.h"

#include <QtCore/QJsonObject>
#include <QtCore/QUrl>

#include <expected>

class QNetworkRequest;

namespace karness {

// The per-provider seam behind StreamingProvider (docs/adr/0019 decision 3).
// Pure and STATELESS — one instance serves every turn, so it holds no
// per-turn state (that lives in StreamingProvider's Turn). Each dialect maps
// the canonical InferenceRequest to its wire format and each framed SSE event
// to the shared DecodedChunk. The transport owns sockets, the stall guard,
// the error taxonomy and the streaming contract; the dialect owns the JSON.
class Dialect {
public:
    virtual ~Dialect() = default;

    // POST target for this turn; baseUrl is the host-configured endpoint root.
    [[nodiscard]] virtual QUrl endpoint(const QUrl &baseUrl,
                                        const InferenceRequest &request) const = 0;

    // Provider auth / version headers. Content-Type and Accept are set by the
    // transport; this adds e.g. Authorization, x-api-key, anthropic-version.
    virtual void applyAuth(QNetworkRequest &netRequest, const QString &apiKey) const = 0;

    // Request body. std::unexpected for an unencodable request (e.g. an
    // ImageBlock before vision support) — the transport touches no socket on failure.
    [[nodiscard]] virtual std::expected<QJsonObject, AgentError>
    encodeRequest(const InferenceRequest &request) const = 0;

    // One framed SSE event -> stream events (+ latched stopReason/usage).
    // Frames the dialect ignores (keep-alives, pings) yield an empty chunk.
    [[nodiscard]] virtual std::expected<DecodedChunk, AgentError>
    decodeEvent(const ServerSentEvent &event) const = 0;

    // True for the dialect's stream-terminating sentinel (the OpenAI Chat
    // Completions / Responses "[DONE]" line). Event-typed dialects (Anthropic,
    // Gemini) leave this false and complete on a stopReason latched at stream
    // end — the transport's lenient "clean end with a stop reason" path.
    [[nodiscard]] virtual bool isTerminalSentinel(const ServerSentEvent &event) const
    {
        Q_UNUSED(event);
        return false;
    }

    // True only for the compat dialect, which lifts <think>…</think> out of a
    // local model's plain text into reasoning. Native dialects stream
    // reasoning explicitly and must not run the splitter over their text.
    [[nodiscard]] virtual bool extractsThinkTags() const { return false; }
};

} // namespace karness
