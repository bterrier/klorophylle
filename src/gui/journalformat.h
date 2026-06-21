// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "journalentry.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

// Presentation helpers for the journal — kept in C++ (not QML/JS) so the labels are
// translated and unit-testable in one place. They live in the UI layer because a
// human-facing label is a presentation concern, not a storage one.
namespace klr {

QString journalKindLabel(JournalEntryKind kind);
QStringList journalKindLabels(); // in JournalEntryKind enum order

// A one-line summary of a (possibly multi-line, possibly markdown) note — the first
// non-blank line, trimmed. Lets the journal list show a git-commit-style subject while the
// full note (markdown) is read in the entry view. Empty/whitespace-only notes → empty.
QString journalNoteSummary(const QString &note);

} // namespace klr
