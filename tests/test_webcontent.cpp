// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "webcontent.h"

using namespace klr::webcontent;

// The pure helpers behind read_online_plant_db (docs/adr/0023): source tokens + the
// allowlist, WatchFlower's species->slug rule, URL building, and the HTML -> text reduction.
// All network-free and deterministic.
class TestWebContent : public QObject {
    Q_OBJECT

private slots:
    void sourceTokenRoundTrips()
    {
        QCOMPARE(sourceToken(Source::Wikipedia), QStringLiteral("wikipedia"));
        QCOMPARE(sourceToken(Source::Wikispecies), QStringLiteral("wikispecies"));
        QCOMPARE(sourceFromToken(u"wikipedia"), std::optional<Source>(Source::Wikipedia));
        QCOMPARE(sourceFromToken(u"wikispecies"), std::optional<Source>(Source::Wikispecies));
    }

    void unknownSourceTokenIsNullopt()
    {
        QCOMPARE(sourceFromToken(u"hortipedia"), std::nullopt);
        QCOMPARE(sourceFromToken(u""), std::nullopt);
        QCOMPARE(sourceFromToken(u"Wikipedia"), std::nullopt); // case-sensitive wire token
    }

    void slugFollowsLegacyRule()
    {
        // WatchFlower: trim, then spaces -> underscores (Plant::name_botanical_url).
        QCOMPARE(speciesToSlug(QStringLiteral("Aloe vera")), QStringLiteral("Aloe_vera"));
        QCOMPARE(speciesToSlug(QStringLiteral("  Ficus lyrata  ")), QStringLiteral("Ficus_lyrata"));
        QCOMPARE(speciesToSlug(QStringLiteral("Abelia")), QStringLiteral("Abelia"));
    }

    void sourceUrlBuildsPerSource()
    {
        const std::optional<QUrl> wiki = sourceUrl(Source::Wikipedia, QStringLiteral("Aloe vera"));
        QVERIFY(wiki.has_value());
        QCOMPARE(wiki->toString(), QStringLiteral("https://en.wikipedia.org/wiki/Aloe_vera"));

        const std::optional<QUrl> sp = sourceUrl(Source::Wikispecies, QStringLiteral("Aloe vera"));
        QVERIFY(sp.has_value());
        QCOMPARE(sp->toString(), QStringLiteral("https://species.wikimedia.org/wiki/Aloe_vera"));
    }

    void sourceUrlPercentEncodesNonAscii()
    {
        const std::optional<QUrl> url = sourceUrl(Source::Wikipedia, QStringLiteral("Aloë"));
        QVERIFY(url.has_value());
        // The host stays clear; the path segment is percent-encoded in the wire (encoded) form.
        QCOMPARE(url->host(), QStringLiteral("en.wikipedia.org"));
        QCOMPARE(QString::fromLatin1(url->toEncoded()),
                 QStringLiteral("https://en.wikipedia.org/wiki/Alo%C3%AB"));
    }

    void blankQueryHasNoUrl()
    {
        QCOMPARE(sourceUrl(Source::Wikipedia, QString()), std::nullopt);
        QCOMPARE(sourceUrl(Source::Wikipedia, QStringLiteral("   ")), std::nullopt);
    }

    void allowlistAcceptsBothSourcesRejectsOthers()
    {
        QVERIFY(isAllowedHost(QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Rose"))));
        QVERIFY(isAllowedHost(QUrl(QStringLiteral("https://species.wikimedia.org/wiki/Rosa"))));
        QVERIFY(!isAllowedHost(QUrl(QStringLiteral("https://evil.example.com/wiki/Rose"))));
        // A look-alike subdomain is not the allowlisted host.
        QVERIFY(!isAllowedHost(QUrl(QStringLiteral("https://en.wikipedia.org.evil.com/x"))));
        QVERIFY(!isAllowedHost(QUrl(QStringLiteral("https://de.wikipedia.org/wiki/Rose"))));
    }

    void htmlToTextStripsTagsAndDecodesEntities()
    {
        const QString html = QStringLiteral(
            "<p>Aloe vera is a <b>succulent</b> &amp; needs &lt;little&gt; water.</p>");
        QCOMPARE(htmlToText(html),
                 QStringLiteral("Aloe vera is a succulent & needs <little> water."));
    }

    void htmlToTextDropsScriptAndStyle()
    {
        const QString html = QStringLiteral(
            "<style>.x{color:red}</style><p>Care</p><script>alert('x')</script><p>Light</p>");
        QCOMPARE(htmlToText(html), QStringLiteral("Care\nLight"));
    }

    void htmlToTextDropsCommentsAndCollapsesWhitespace()
    {
        const QString html = QStringLiteral(
            "<!-- nav -->\n<div>  Water   often.\t</div>\n\n\n<div>Bright   light.</div>");
        QCOMPARE(htmlToText(html), QStringLiteral("Water often.\nBright light."));
    }

    void htmlToTextDecodesNumericRefs()
    {
        // decimal + hex numeric character references
        QCOMPARE(htmlToText(QStringLiteral("38&#176;C and &#x2013; dash")),
                 QString::fromUtf8("38°C and – dash"));
    }

    void htmlToTextKeepsUnknownEntityLiteral()
    {
        QCOMPARE(htmlToText(QStringLiteral("a &bogus; b")), QStringLiteral("a &bogus; b"));
    }

    void textBudgetIsReasonable()
    {
        QVERIFY(kTextBudget >= 4000);
    }
};

QTEST_MAIN(TestWebContent)
#include "test_webcontent.moc"
