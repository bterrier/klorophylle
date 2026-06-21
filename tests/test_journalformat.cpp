// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "journalformat.h"

using namespace klr;

// journalNoteSummary: the git-commit-style subject line the journal list shows for a
// (possibly multi-line, possibly markdown) note. The full note is read in the entry view.
class TestJournalFormat : public QObject {
    Q_OBJECT

private slots:
    void singleLinePassesThrough()
    {
        QCOMPARE(journalNoteSummary(QStringLiteral("Watered well today")),
                 QStringLiteral("Watered well today"));
    }

    void multiLineKeepsFirstLineOnly()
    {
        QCOMPARE(journalNoteSummary(QStringLiteral("# Repotting\n\nMoved to a bigger pot.")),
                 QStringLiteral("# Repotting"));
    }

    void leadingBlankLinesSkipped()
    {
        QCOMPARE(journalNoteSummary(QStringLiteral("\n\n   \nFirst real line\nsecond")),
                 QStringLiteral("First real line"));
    }

    void firstLineIsTrimmed()
    {
        QCOMPARE(journalNoteSummary(QStringLiteral("   hello   \nworld")),
                 QStringLiteral("hello"));
    }

    void emptyOrWhitespaceYieldsEmpty()
    {
        QCOMPARE(journalNoteSummary(QString()), QString());
        QCOMPARE(journalNoteSummary(QStringLiteral("   \n\t\n  ")), QString());
    }
};

QTEST_MAIN(TestJournalFormat)
#include "test_journalformat.moc"
