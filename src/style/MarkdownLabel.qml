// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style read-only markdown renderer — extends the styled Label, rendering
// Text.MarkdownText. Used by the read-only views (chat bubbles, journal entry view): the
// user always TYPES plain text into TextField/TextArea; only these read-only labels render
// markdown. Links open in the system browser.
import QtQuick

Label {
    textFormat: Text.MarkdownText
    wrapMode: Text.WordWrap
    onLinkActivated: (link) => Qt.openUrlExternally(link)
}
