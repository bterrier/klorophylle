// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>

#include "diskattachmentfilestore.h"

using namespace klr;

// DiskAttachmentFileStore over a QTemporaryDir: hermetic, real disk I/O. Covers store/read/remove
// and the startup orphan sweep (ADR 0024 decisions 4-5).
class TestFileStore : public QObject {
    Q_OBJECT

    // Write a small source file with the given bytes + extension; return its path.
    static QString writeSource(const QDir &dir, const QString &name, const QByteArray &bytes)
    {
        const QString path = dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(bytes);
        f.close();
        return path;
    }

private slots:
    void storeReadRoundTrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        DiskAttachmentFileStore store(tmp.path());

        const QByteArray bytes("\x89PNG\r\n fake image bytes");
        const QString src = writeSource(QDir(tmp.path()), QStringLiteral("src.png"), bytes);
        const AttachmentId id = AttachmentId::generate();

        const QString ref = store.store(src, id);
        QVERIFY(ref.startsWith(QStringLiteral("attachments/"))); // app-data-relative
        QVERIFY(ref.endsWith(QStringLiteral(".png")));           // extension from the source
        QVERIFY(ref.contains(id.toString()));                    // named by the id

        const std::optional<QByteArray> back = store.read(ref);
        QVERIFY(back.has_value());
        QCOMPARE(*back, bytes);

        // The file physically lives under <base>/attachments/.
        QVERIFY(QFile::exists(store.absolutePath(ref)));
        QVERIFY(store.absolutePath(ref).startsWith(tmp.path()));
    }

    void readMissingIsNullopt()
    {
        QTemporaryDir tmp;
        DiskAttachmentFileStore store(tmp.path());
        // An absent file is nullopt, not an error (restored-backup tolerance).
        QVERIFY(!store.read(QStringLiteral("attachments/nope.jpg")).has_value());
    }

    void removeIsIdempotent()
    {
        QTemporaryDir tmp;
        DiskAttachmentFileStore store(tmp.path());
        const QByteArray bytes("data");
        const QString src = writeSource(QDir(tmp.path()), QStringLiteral("s.jpg"), bytes);
        const QString ref = store.store(src, AttachmentId::generate());
        QVERIFY(store.read(ref).has_value());

        store.remove(ref);
        QVERIFY(!store.read(ref).has_value());
        store.remove(ref); // again — must not throw
        store.remove(QStringLiteral("attachments/never-existed.jpg")); // must not throw
    }

    void sweepDeletesOrphansKeepsLive()
    {
        QTemporaryDir tmp;
        DiskAttachmentFileStore store(tmp.path());
        const QByteArray bytes("x");
        const QString live = store.store(writeSource(QDir(tmp.path()), QStringLiteral("a.jpg"), bytes),
                                         AttachmentId::generate());
        const QString orphan = store.store(writeSource(QDir(tmp.path()), QStringLiteral("b.jpg"), bytes),
                                           AttachmentId::generate());

        // Only `live` is known to the repository; `orphan` has no row.
        const int removed = store.sweepOrphans(QSet<QString>{ live });
        QCOMPARE(removed, 1);
        QVERIFY(store.read(live).has_value());     // live file kept
        QVERIFY(!store.read(orphan).has_value());  // orphan swept
    }

    void sweepNoAttachmentsDirIsSafe()
    {
        QTemporaryDir tmp;
        DiskAttachmentFileStore store(tmp.path());
        // Nothing stored yet — the attachments dir doesn't exist; sweep is a no-op, not a crash.
        QCOMPARE(store.sweepOrphans(QSet<QString>{}), 0);
    }
};

QTEST_GUILESS_MAIN(TestFileStore)
#include "test_filestore.moc"
