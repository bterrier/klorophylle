// SPDX-License-Identifier: GPL-3.0-or-later
#include "webcontent.h"

#include <QtCore/QHash>
#include <QtCore/QRegularExpression>

namespace klr::webcontent {

namespace {

// Resolve a single entity body (the text between '&' and ';') to its replacement, or a null
// QString when unrecognized (so the caller keeps the literal `&...;`). Handles numeric refs
// (&#1234; / &#x1F33F;) and a small table of the named entities common in encyclopedia prose.
QString resolveEntity(const QString &body)
{
    if (body.startsWith(QLatin1Char('#'))) {
        bool ok = false;
        char32_t code = 0;
        if (body.size() > 1 && (body[1] == QLatin1Char('x') || body[1] == QLatin1Char('X')))
            code = body.mid(2).toUInt(&ok, 16);
        else
            code = body.mid(1).toUInt(&ok, 10);
        if (!ok || code == 0 || code > 0x10FFFF)
            return QString();
        return QString::fromUcs4(&code, 1);
    }

    // nbsp maps to a normal space so the whitespace-collapse below folds it away.
    static const QHash<QString, QString> named = {
        {QStringLiteral("amp"), QStringLiteral("&")},   {QStringLiteral("lt"), QStringLiteral("<")},
        {QStringLiteral("gt"), QStringLiteral(">")},    {QStringLiteral("quot"), QStringLiteral("\"")},
        {QStringLiteral("apos"), QStringLiteral("'")},  {QStringLiteral("nbsp"), QStringLiteral(" ")},
        {QStringLiteral("mdash"), QString::fromUtf8("—")},
        {QStringLiteral("ndash"), QString::fromUtf8("–")},
        {QStringLiteral("hellip"), QString::fromUtf8("…")},
        {QStringLiteral("lsquo"), QString::fromUtf8("‘")},
        {QStringLiteral("rsquo"), QString::fromUtf8("’")},
        {QStringLiteral("ldquo"), QString::fromUtf8("“")},
        {QStringLiteral("rdquo"), QString::fromUtf8("”")},
        {QStringLiteral("times"), QString::fromUtf8("×")},
        {QStringLiteral("deg"), QString::fromUtf8("°")},
        {QStringLiteral("middot"), QString::fromUtf8("·")},
    };
    return named.value(body); // default-constructed (null) QString when absent
}

QString decodeEntities(const QString &in)
{
    static const QRegularExpression ent(QStringLiteral("&(#[xX]?[0-9a-fA-F]+|[a-zA-Z][a-zA-Z0-9]*);"));
    QString out;
    out.reserve(in.size());
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = ent.globalMatch(in);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(in).mid(last, m.capturedStart() - last);
        const QString rep = resolveEntity(m.captured(1));
        out += rep.isNull() ? m.captured(0) : rep;
        last = m.capturedEnd();
    }
    out += QStringView(in).mid(last);
    return out;
}

} // namespace

QString sourceToken(Source source)
{
    switch (source) {
    case Source::Wikipedia:
        return QStringLiteral("wikipedia");
    case Source::Wikispecies:
        return QStringLiteral("wikispecies");
    }
    return QString();
}

std::optional<Source> sourceFromToken(QStringView token)
{
    if (token == QLatin1String("wikipedia"))
        return Source::Wikipedia;
    if (token == QLatin1String("wikispecies"))
        return Source::Wikispecies;
    return std::nullopt;
}

QString sourceHost(Source source)
{
    switch (source) {
    case Source::Wikipedia:
        return QStringLiteral("en.wikipedia.org");
    case Source::Wikispecies:
        return QStringLiteral("species.wikimedia.org");
    }
    return QString();
}

QString speciesToSlug(const QString &species)
{
    return species.trimmed().replace(QLatin1Char(' '), QLatin1Char('_'));
}

std::optional<QUrl> sourceUrl(Source source, const QString &query)
{
    const QString slug = speciesToSlug(query);
    if (slug.isEmpty())
        return std::nullopt;
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(sourceHost(source));
    url.setPath(QStringLiteral("/wiki/") + slug); // QUrl percent-encodes the segment on output
    return url;
}

bool isAllowedHost(const QUrl &url)
{
    static const QStringList allowed = {QStringLiteral("en.wikipedia.org"),
                                        QStringLiteral("species.wikimedia.org")};
    return allowed.contains(url.host());
}

QString htmlToText(QStringView html)
{
    QString s = html.toString();

    static const QRegularExpression scriptStyle(
        QStringLiteral("<(script|style)\\b[^>]*>.*?</\\1>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    s.remove(scriptStyle);

    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"),
                                             QRegularExpression::DotMatchesEverythingOption);
    s.remove(comments);

    // Block-level breaks become newlines so paragraphs/headings/list items stay separated.
    static const QRegularExpression blocks(
        QStringLiteral("<(br|/p|/div|/h[1-6]|/li|/tr|/table|/section|/article|/blockquote)\\b[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    s.replace(blocks, QStringLiteral("\n"));

    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    s.remove(tags);

    s = decodeEntities(s);

    // Collapse intra-line whitespace, then collapse blank-line runs to a single blank line.
    static const QRegularExpression inlineWs(QStringLiteral("[ \\t\\f\\r]+"));
    s.replace(inlineWs, QStringLiteral(" "));
    static const QRegularExpression blankLines(QStringLiteral(" *\\n[ \\n]*"));
    s.replace(blankLines, QStringLiteral("\n"));

    return s.trimmed();
}

} // namespace klr::webcontent
