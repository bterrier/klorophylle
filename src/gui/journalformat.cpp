// SPDX-License-Identifier: GPL-3.0-or-later
#include "journalformat.h"

#include <QtCore/QCoreApplication>

namespace klr {

QString journalKindLabel(JournalEntryKind kind)
{
    switch (kind) {
    case JournalEntryKind::Note:        return QCoreApplication::translate("journal", "Note");
    case JournalEntryKind::Watering:    return QCoreApplication::translate("journal", "Watering");
    case JournalEntryKind::Fertilizing: return QCoreApplication::translate("journal", "Fertilizing");
    case JournalEntryKind::Repotting:   return QCoreApplication::translate("journal", "Repotting");
    case JournalEntryKind::Pruning:     return QCoreApplication::translate("journal", "Pruning");
    case JournalEntryKind::Observation: return QCoreApplication::translate("journal", "Observation");
    case JournalEntryKind::Memory:      return QCoreApplication::translate("journal", "Memory");
    }
    return {};
}

QString journalNoteSummary(const QString &note)
{
    const auto lines = note.split(u'\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return {};
}

QStringList journalKindLabels()
{
    return {
        journalKindLabel(JournalEntryKind::Note),
        journalKindLabel(JournalEntryKind::Watering),
        journalKindLabel(JournalEntryKind::Fertilizing),
        journalKindLabel(JournalEntryKind::Repotting),
        journalKindLabel(JournalEntryKind::Pruning),
        journalKindLabel(JournalEntryKind::Observation),
        journalKindLabel(JournalEntryKind::Memory),
    };
}

} // namespace klr
