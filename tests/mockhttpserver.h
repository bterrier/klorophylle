// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtNetwork/QTcpServer>

class QTcpSocket;

// Minimal scripted HTTP/1.1 server for provider conformance tests (ADR 0019
// decision 3: captured streams replayed through a local mock — no network in
// ctest, loopback only). Replies with Transfer-Encoding: chunked, which is
// load-bearing: a clean end is the terminating 0-chunk, and dropAfterChunks
// closes the socket WITHOUT it, giving the client a distinguishable
// mid-stream connection drop (close-delimited bodies cannot express that).
// Script chunks are written one event-loop pass apart so the client is
// forced through multiple readyRead slices at whatever hostile offsets the
// test chose.
class MockHttpServer : public QObject {
    Q_OBJECT
public:
    struct Script {
        int status = 200;
        QByteArray statusText = "OK";
        QByteArray contentType = "text/event-stream";
        QList<QByteArray> chunks;     // each written as one HTTP chunk
        bool dropAfterChunks = false; // close without the 0-chunk (TCP-level drop)
        bool holdOpenAtEnd = false;   // send chunks then stall (cancel/timeout tests)
    };

    explicit MockHttpServer(QObject *parent = nullptr);

    [[nodiscard]] bool start(); // 127.0.0.1, ephemeral port
    [[nodiscard]] QUrl baseUrl() const; // http://127.0.0.1:<port>/v1
    void setScript(Script script);

    [[nodiscard]] QByteArray lastRequestHeaders() const { return m_lastHeaders; }
    [[nodiscard]] QByteArray lastRequestBody() const { return m_lastBody; }

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void respond(QTcpSocket *socket);
    void writeNextChunk(QTcpSocket *socket, qsizetype index);

    QTcpServer m_server;
    Script m_script;
    QByteArray m_pending; // request bytes of the connection being read
    QByteArray m_lastHeaders;
    QByteArray m_lastBody;
};
