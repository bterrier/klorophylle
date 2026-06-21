// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "sseparser.h"

using namespace karness;

class TestSseParser : public QObject {
    Q_OBJECT
private slots:
    void singleEventLf()
    {
        SseParser parser;
        const auto events = parser.feed("data: hello\n\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first(), (ServerSentEvent{QString(), QStringLiteral("hello"), QString()}));
    }

    void crlfTerminators()
    {
        SseParser parser;
        const auto events = parser.feed("data: hello\r\n\r\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("hello"));
    }

    void chunkSplitMidLine()
    {
        SseParser parser;
        QVERIFY(parser.feed("data: hel").isEmpty());
        const auto events = parser.feed("lo\n\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("hello"));
    }

    void chunkSplitMidCrlf()
    {
        SseParser parser;
        QVERIFY(parser.feed("data: a\r").isEmpty());
        const auto events = parser.feed("\n\r\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("a"));
    }

    void chunkSplitMidUtf8()
    {
        SseParser parser;
        const QByteArray payload = QStringLiteral("data: café\n\n").toUtf8();
        // Split inside the two-byte UTF-8 sequence of 'é'.
        const qsizetype cut = payload.size() - 4;
        QVERIFY(parser.feed(QByteArrayView(payload).first(cut)).isEmpty());
        const auto events = parser.feed(QByteArrayView(payload).sliced(cut));
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("café"));
    }

    void multipleDataLinesJoined()
    {
        SseParser parser;
        const auto events = parser.feed("data: line one\ndata: line two\n\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("line one\nline two"));
    }

    void leadingSpaceStrippedOnce()
    {
        SseParser parser;
        // "data:x" (no space) and "data:  x" (two spaces: one is syntax, one payload).
        const auto events = parser.feed("data:no space\n\ndata:  padded\n\n");
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.at(0).data, QStringLiteral("no space"));
        QCOMPARE(events.at(1).data, QStringLiteral(" padded"));
    }

    void commentLinesSkipped()
    {
        SseParser parser;
        const auto events = parser.feed(": OPENROUTER PROCESSING\ndata: x\n\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("x"));
    }

    void commentOnlyBlockNotDispatched()
    {
        SseParser parser;
        // Keepalive comment followed by a blank line must not produce an event.
        QVERIFY(parser.feed(": keepalive\n\n").isEmpty());
    }

    void eventAndIdCaptured()
    {
        SseParser parser;
        const auto events = parser.feed("event: message_start\nid: 7\ndata: x\n\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().event, QStringLiteral("message_start"));
        QCOMPARE(events.first().id, QStringLiteral("7"));
    }

    void idPersistsAcrossEvents()
    {
        SseParser parser;
        const auto events = parser.feed("id: 7\ndata: a\n\ndata: b\n\n");
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.at(1).id, QStringLiteral("7")); // last-event-ID, per spec
    }

    void eventTypeResetsAfterDispatch()
    {
        SseParser parser;
        const auto events = parser.feed("event: ping\ndata: a\n\ndata: b\n\n");
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.at(0).event, QStringLiteral("ping"));
        QCOMPARE(events.at(1).event, QString());
    }

    void eventFieldWithoutDataNotDispatched()
    {
        SseParser parser;
        QVERIFY(parser.feed("event: ping\n\n").isEmpty()); // spec: no data, no dispatch
        // And the buffered event type must not leak into the next event.
        const auto events = parser.feed("data: x\n\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().event, QString());
    }

    void emptyDataLineDispatchesEmptyEvent()
    {
        SseParser parser;
        const auto events = parser.feed("data:\n\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QString());
    }

    void retryAndUnknownFieldsIgnored()
    {
        SseParser parser;
        const auto events = parser.feed("retry: 3000\nfoo: bar\ndata: x\n\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("x"));
    }

    void twoEventsInOneFeed()
    {
        SseParser parser;
        const auto events = parser.feed("data: a\n\ndata: b\n\n");
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.at(0).data, QStringLiteral("a"));
        QCOMPARE(events.at(1).data, QStringLiteral("b"));
    }

    void eventAcrossThreeFeeds()
    {
        SseParser parser;
        QVERIFY(parser.feed("da").isEmpty());
        QVERIFY(parser.feed("ta: hello\n").isEmpty());
        const auto events = parser.feed("\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("hello"));
    }

    void trailingIncompleteEventDiscarded()
    {
        SseParser parser;
        // No blank line ever arrives: the open event is never dispatched.
        QVERIFY(parser.feed("data: orphan\n").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSseParser)
#include "test_sseparser.moc"
