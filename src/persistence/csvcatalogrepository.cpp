// SPDX-License-Identifier: GPL-3.0-or-later
#include "csvcatalogrepository.h"

#include "catalogcsv.h"

#include <QtCore/QFile>

#include <algorithm>

namespace klr {

namespace {

// Lower-case and strip diacritics, so "ficus" matches "Fícus" and "Aloe" matches
// "aloë". NFD decomposes accented letters into base + combining mark; we drop the
// marks.
QString fold(const QString &s)
{
    const QString d = s.normalized(QString::NormalizationForm_D).toLower();
    QString out;
    out.reserve(d.size());
    for (const QChar c : d) {
        const QChar::Category cat = c.category();
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining
            || cat == QChar::Mark_Enclosing)
            continue;
        out.append(c);
    }
    return out;
}

} // namespace

CsvCatalogRepository::CsvCatalogRepository(QList<CatalogEntry> entries)
    : m_entries(std::move(entries))
{
    m_folded.reserve(m_entries.size());
    m_indexByKey.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const CatalogEntry &e = m_entries.at(i);
        m_folded.append({ fold(e.key), fold(e.commonName) });
        m_indexByKey.insert(e.key, i);
    }
}

QList<CatalogEntry> CsvCatalogRepository::search(const QString &query, int limit) const
{
    const QString q = fold(query.trimmed());
    if (q.isEmpty() || limit <= 0)
        return {};

    struct Scored {
        int rank;
        int index;
    };
    QList<Scored> hits;
    for (int i = 0; i < m_folded.size(); ++i) {
        const Folded &f = m_folded.at(i);
        int rank;
        if (f.key.startsWith(q))
            rank = 0;
        else if (!f.commonName.isEmpty() && f.commonName.startsWith(q))
            rank = 1;
        else if (f.key.contains(q))
            rank = 2;
        else if (f.commonName.contains(q))
            rank = 3;
        else
            continue;
        hits.append({ rank, i });
    }

    // Stable: ties (same rank) fall back to botanical alphabetical order.
    std::stable_sort(hits.begin(), hits.end(), [this](const Scored &a, const Scored &b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return m_entries.at(a.index).key < m_entries.at(b.index).key;
    });

    QList<CatalogEntry> out;
    const int n = std::min<int>(limit, hits.size());
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.append(m_entries.at(hits.at(i).index));
    return out;
}

std::optional<CatalogEntry> CsvCatalogRepository::byKey(const QString &key) const
{
    const auto it = m_indexByKey.constFind(key);
    if (it == m_indexByKey.constEnd())
        return std::nullopt;
    return m_entries.at(*it);
}

CsvCatalogRepository loadBundledCatalog()
{
    QFile f(QStringLiteral(":/klr/catalog/plantdb.csv"));
    if (!f.open(QIODevice::ReadOnly))
        return CsvCatalogRepository({});
    return CsvCatalogRepository(CatalogCsv::parse(f.readAll()));
}

} // namespace klr
