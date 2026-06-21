// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringView>
#include <QtCore/QUrl>

#include <optional>

// Pure helpers behind the web tool read_online_plant_db (docs/adr/0023): the host-curated
// source allowlist + URL building, and the HTML -> readable-text reduction. All deterministic and
// network-free, so they unit-test without a fetcher. The model never picks a URL — it picks a
// SOURCE by name; the host owns the domain + URL template (decision 1).
namespace klr::webcontent {

// The reputable plant encyclopedias the web tool may fetch (decision 1). The model selects one by
// its token ("wikipedia" | "wikispecies"); the host maps it to a domain + URL template.
enum class Source { Wikipedia, Wikispecies };

// The wire token for a source ("wikipedia" | "wikispecies").
[[nodiscard]] QString sourceToken(Source source);

// Parse a source token to its enum, or nullopt when unknown (the model passed a bad name).
[[nodiscard]] std::optional<Source> sourceFromToken(QStringView token);

// The host of a source ("en.wikipedia.org" | "species.wikimedia.org") — also its provenance label.
[[nodiscard]] QString sourceHost(Source source);

// WatchFlower's slug rule (Plant::name_botanical_url): trim, then spaces -> underscores.
// Percent-encoding of the resulting path segment is left to QUrl in sourceUrl().
[[nodiscard]] QString speciesToSlug(const QString &species);

// Build the article URL for `query` on `source` (https://<host>/wiki/<slug>), or nullopt when
// `query` is blank. QUrl percent-encodes the path segment on output.
[[nodiscard]] std::optional<QUrl> sourceUrl(Source source, const QString &query);

// True when `url`'s host is in the built-in allowlist — checked when building the request and again
// on the final URL after redirects (defense in depth: a redirect must not escape the allowlist).
[[nodiscard]] bool isAllowedHost(const QUrl &url);

// Reduce HTML to readable plain text: drop <script>/<style> blocks and comments, turn block-level
// breaks into newlines, strip the remaining tags, decode common HTML entities, and collapse runs of
// whitespace. The web tool truncates the result to kTextBudget before it enters context (decision 3).
[[nodiscard]] QString htmlToText(QStringView html);

// The character budget the web tool truncates reduced page text to before returning it (decision 3).
inline constexpr qsizetype kTextBudget = 8000;

} // namespace klr::webcontent
