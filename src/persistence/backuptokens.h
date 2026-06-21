// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "journalentry.h" // JournalEntryKind
#include "reading.h"      // Quantity, Unit, Provenance
#include "sensor.h"       // HandleKind

#include <QtCore/QString>

#include <optional>

// Stable string tokens for the enums that cross the backup JSON boundary (ADR 0010).
// We serialize tokens, NEVER the underlying ints: an int shifts the moment an enumerator
// is reordered or inserted (and Quantity explicitly reserves "keep last"), silently
// corrupting old backups; a token is reorder-proof and legible in the file. The token
// IS the enumerator name.
//
// Coverage is total by construction: every toToken() is an exhaustive switch with no
// default (so a new enumerator fails to compile under -Wswitch), and test_backuptokens
// round-trips every value of all five enums.
namespace klr::backuptokens {

QString toToken(Quantity q);
QString toToken(Unit u);
QString toToken(Provenance p);
QString toToken(JournalEntryKind k);
QString toToken(HandleKind h);

// Unknown / unrecognised token -> nullopt, so the importer can warn-and-skip a row
// written by a newer version rather than abort the whole restore.
template <class E>
std::optional<E> fromToken(const QString &token);

template <> std::optional<Quantity> fromToken<Quantity>(const QString &token);
template <> std::optional<Unit> fromToken<Unit>(const QString &token);
template <> std::optional<Provenance> fromToken<Provenance>(const QString &token);
template <> std::optional<JournalEntryKind> fromToken<JournalEntryKind>(const QString &token);
template <> std::optional<HandleKind> fromToken<HandleKind>(const QString &token);

} // namespace klr::backuptokens
