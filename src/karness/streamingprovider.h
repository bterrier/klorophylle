// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dialect.h"
#include "iprovider.h"
#include "messageaccumulator.h"
#include "providerconfig.h"
#include "sseparser.h"
#include "thinktagsplitter.h"

#include <QtCore/QObject>
#include <QtCore/QPromise>
#include <QtNetwork/QNetworkAccessManager>

#include <memory>
#include <optional>

class QNetworkReply;

namespace karness {

// The streaming-HTTP transport shared by every SSE-based provider dialect
// (docs/adr/0019 decision 3). Owns sockets, the stall-timeout guard, the
// HTTP/network/cancel/timeout error taxonomy and the single-terminal +
// destruction guarantees of iprovider.h; an injected Dialect supplies the
// endpoint, auth headers, request encoding and per-event decoding. SSE
// framing is the shared parser, <think> extraction the splitter (only when
// the dialect asks for it), message assembly the accumulator. One turn in
// flight at a time; a second generate() fails fast with Code::Provider.
// Concrete providers (OpenAiCompatProvider, AnthropicProvider, …) are thin
// subclasses that pass their Dialect to this constructor.
class StreamingProvider : public QObject, public IProvider {
    Q_OBJECT
public:
    StreamingProvider(ProviderConfig config, std::unique_ptr<Dialect> dialect,
                      QObject *parent = nullptr);
    ~StreamingProvider() override; // in-flight turn terminates Cancelled, future finishes

    ModelCaps caps() const override;
    bool isReady() const override;
    QFuture<StreamEvent> generate(const InferenceRequest &request) override;
    void cancel() override;

private:
    struct Turn {
        QPromise<StreamEvent> promise;
        SseParser sse;
        ThinkTagSplitter think; // only fed when the dialect extracts <think>
        MessageAccumulator accumulator;
        std::optional<StopReason> stopReason; // latched from finish/stop frames
        std::optional<TokenUsage> usage;      // latched from the usage frame
        QByteArray errorBody;                 // buffered body when httpStatus != 2xx
        int httpStatus = 0;
        bool terminalEmitted = false;
    };

    void onReadyRead();
    void onFinished();
    void emitEvents(const QList<StreamEvent> &events); // promise + accumulator
    void finishWithDone();
    void finishWithError(AgentError error);
    void teardownTransport();
    static AgentError httpError(int status, const QByteArray &body);

    ProviderConfig m_config;
    std::unique_ptr<Dialect> m_dialect;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<Turn> m_turn;
    // A transfer-timeout abort and cancel() both surface as
    // OperationCanceledError; this flag tells Cancelled from Timeout.
    bool m_cancelRequested = false;
};

} // namespace karness
