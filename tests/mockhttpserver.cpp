// SPDX-License-Identifier: GPL-3.0-or-later
#include "mockhttpserver.h"

#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>

MockHttpServer::MockHttpServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &MockHttpServer::onNewConnection);
}

bool MockHttpServer::start()
{
    return m_server.listen(QHostAddress::LocalHost, 0);
}

QUrl MockHttpServer::baseUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort()));
}

void MockHttpServer::setScript(Script script)
{
    m_script = std::move(script);
}

void MockHttpServer::onNewConnection()
{
    QTcpSocket *socket = m_server.nextPendingConnection();
    m_pending.clear();
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void MockHttpServer::onReadyRead(QTcpSocket *socket)
{
    m_pending += socket->readAll();
    const qsizetype headersEnd = m_pending.indexOf("\r\n\r\n");
    if (headersEnd < 0)
        return;
    const QByteArray headers = m_pending.first(headersEnd);
    qsizetype contentLength = 0;
    for (const QByteArray &line : headers.split('\r'))
        if (line.trimmed().toLower().startsWith("content-length:"))
            contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
    const QByteArray body = m_pending.sliced(headersEnd + 4);
    if (body.size() < contentLength)
        return; // body still streaming in
    m_lastHeaders = headers;
    m_lastBody = body.first(contentLength);
    socket->disconnect(this); // one request per connection; ignore any more bytes
    respond(socket);
}

void MockHttpServer::respond(QTcpSocket *socket)
{
    QByteArray head = "HTTP/1.1 " + QByteArray::number(m_script.status) + " " + m_script.statusText
        + "\r\n"
          "Content-Type: "
        + m_script.contentType
        + "\r\n"
          "Transfer-Encoding: chunked\r\n"
          "Connection: close\r\n\r\n";
    socket->write(head);
    writeNextChunk(socket, 0);
}

void MockHttpServer::writeNextChunk(QTcpSocket *socket, qsizetype index)
{
    if (index >= m_script.chunks.size()) {
        if (m_script.holdOpenAtEnd)
            return; // stall: the client decides (cancel / timeout)
        if (m_script.dropAfterChunks) {
            socket->disconnectFromHost(); // no 0-chunk: a mid-stream drop
            return;
        }
        socket->write("0\r\n\r\n"); // clean chunked end
        socket->disconnectFromHost();
        return;
    }
    const QByteArray &chunk = m_script.chunks.at(index);
    socket->write(QByteArray::number(chunk.size(), 16) + "\r\n" + chunk + "\r\n");
    // Next chunk one event-loop pass later, so the client sees separate
    // readyRead slices (socket as context: a vanished client stops the chain).
    QTimer::singleShot(0, socket, [this, socket, index] { writeNextChunk(socket, index + 1); });
}
