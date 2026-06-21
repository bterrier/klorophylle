// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "confirmingtool.h"
#include "faketool.h"

using namespace klr;
using namespace karness;

namespace {

QString textOf(const ToolOutcome &o)
{
    return o.parts.isEmpty() ? QString() : std::get<TextBlock>(o.parts.first()).text;
}

} // namespace

// ConfirmingTool wraps an inner tool: invoke() stays pending and emits confirmationRequested;
// approve() forwards the inner outcome; reject() yields an isError the model recovers from.
class TestConfirmingTool : public QObject {
    Q_OBJECT

private slots:
    void specPassesThrough()
    {
        FakeTool inner(QStringLiteral("add_journal_entry"));
        ConfirmingTool tool(inner);
        QCOMPARE(tool.spec().name, QStringLiteral("add_journal_entry"));
    }

    void invokeStaysPendingAndRequestsConfirmation()
    {
        FakeTool inner(QStringLiteral("add_journal_entry"));
        ConfirmingTool tool(inner, [](const QJsonObject &a) {
            return QStringLiteral("Add note: ") + a.value(QStringLiteral("note")).toString();
        });
        QSignalSpy spy(&tool, &ConfirmingTool::confirmationRequested);

        QFuture<ToolOutcome> f = tool.invoke(QJsonObject{ { QStringLiteral("note"),
                                                            QStringLiteral("watered") } });
        QVERIFY(!f.isFinished());        // pending until the user decides
        QVERIFY(tool.hasPending());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Add note: watered"));
        QVERIFY(inner.invocations().isEmpty()); // inner not run before approval
    }

    void approveRunsInnerAndForwardsOutcome()
    {
        FakeTool inner(QStringLiteral("add_journal_entry"));
        inner.setOutcome(ToolOutcome{ { TextBlock{ QStringLiteral("entry added") } }, false });
        ConfirmingTool tool(inner);

        QFuture<ToolOutcome> f = tool.invoke(QJsonObject{ { QStringLiteral("note"),
                                                            QStringLiteral("x") } });
        tool.approve();

        QTRY_VERIFY(f.isFinished());
        QCOMPARE(inner.invocations().size(), 1);
        QVERIFY(!f.result().isError);
        QCOMPARE(textOf(f.result()), QStringLiteral("entry added"));
        QVERIFY(!tool.hasPending());
    }

    void rejectYieldsErrorWithoutRunningInner()
    {
        FakeTool inner(QStringLiteral("add_journal_entry"));
        ConfirmingTool tool(inner);

        QFuture<ToolOutcome> f = tool.invoke(QJsonObject{});
        tool.reject();

        QTRY_VERIFY(f.isFinished());
        QVERIFY(inner.invocations().isEmpty()); // never executed
        QVERIFY(f.result().isError);
        QVERIFY(!tool.hasPending());
    }

    // A single turn can issue several parallel calls to the wrapped tool. Each is queued and
    // surfaced one at a time, in order — no call is silently dropped.
    void concurrentInvokesPromptSequentially()
    {
        FakeTool inner(QStringLiteral("add_journal_entry"));
        inner.setOutcome(ToolOutcome{ { TextBlock{ QStringLiteral("entry added") } }, false });
        ConfirmingTool tool(inner, [](const QJsonObject &a) {
            return QStringLiteral("Add note: ") + a.value(QStringLiteral("note")).toString();
        });
        QSignalSpy spy(&tool, &ConfirmingTool::confirmationRequested);

        QFuture<ToolOutcome> f1 = tool.invoke(QJsonObject{ { QStringLiteral("note"),
                                                             QStringLiteral("first") } });
        QFuture<ToolOutcome> f2 = tool.invoke(QJsonObject{ { QStringLiteral("note"),
                                                             QStringLiteral("second") } });
        // Both pending; only the head has been surfaced so far.
        QVERIFY(!f1.isFinished());
        QVERIFY(!f2.isFinished());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Add note: first"));
        QVERIFY(inner.invocations().isEmpty());

        tool.approve(); // resolves the head, surfaces the next confirmation
        QTRY_VERIFY(f1.isFinished());
        QVERIFY(!f1.result().isError);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("Add note: second"));
        QVERIFY(!f2.isFinished());

        tool.approve();
        QTRY_VERIFY(f2.isFinished());
        QVERIFY(!f2.result().isError);
        QVERIFY(!tool.hasPending());

        // Inner ran once per approval, in original call order.
        QCOMPARE(inner.invocations().size(), 2);
        QCOMPARE(inner.invocations().at(0).value(QStringLiteral("note")).toString(),
                 QStringLiteral("first"));
        QCOMPARE(inner.invocations().at(1).value(QStringLiteral("note")).toString(),
                 QStringLiteral("second"));
    }

    // Rejecting the head resolves its promise as an error without running the inner tool, then
    // advances to the next queued call, which can still be approved.
    void rejectThenApproveAcrossQueue()
    {
        FakeTool inner(QStringLiteral("add_journal_entry"));
        inner.setOutcome(ToolOutcome{ { TextBlock{ QStringLiteral("entry added") } }, false });
        ConfirmingTool tool(inner);

        QFuture<ToolOutcome> f1 = tool.invoke(QJsonObject{ { QStringLiteral("note"),
                                                             QStringLiteral("first") } });
        QFuture<ToolOutcome> f2 = tool.invoke(QJsonObject{ { QStringLiteral("note"),
                                                             QStringLiteral("second") } });

        tool.reject(); // head only
        QTRY_VERIFY(f1.isFinished());
        QVERIFY(f1.result().isError);
        QVERIFY(inner.invocations().isEmpty()); // rejected call never executed

        tool.approve();
        QTRY_VERIFY(f2.isFinished());
        QVERIFY(!f2.result().isError);
        QCOMPARE(inner.invocations().size(), 1); // only the approved one ran
        QCOMPARE(inner.invocations().at(0).value(QStringLiteral("note")).toString(),
                 QStringLiteral("second"));
        QVERIFY(!tool.hasPending());
    }

    // reset() abandons every queued confirmation (e.g. a cancelled turn): each future finishes
    // so nothing dangles, and the tool is no longer pending.
    void resetAbandonsQueuedConfirmations()
    {
        FakeTool inner(QStringLiteral("add_journal_entry"));
        ConfirmingTool tool(inner);

        QFuture<ToolOutcome> f1 = tool.invoke(QJsonObject{});
        QFuture<ToolOutcome> f2 = tool.invoke(QJsonObject{});
        QVERIFY(tool.hasPending());

        tool.reset();
        QVERIFY(!tool.hasPending());
        QTRY_VERIFY(f1.isFinished());
        QTRY_VERIFY(f2.isFinished());
        QVERIFY(f1.result().isError);
        QVERIFY(f2.result().isError);
        QVERIFY(inner.invocations().isEmpty()); // nothing ran
    }
};

QTEST_GUILESS_MAIN(TestConfirmingTool)
#include "test_confirmingtool.moc"
