// SPDX-License-Identifier: GPL-3.0-or-later
#include "transcript.h"

#include "clock.h"
#include "messagecodec.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QTimeZone>

#include <algorithm>

namespace klr::transcript {

QList<karness::Message> load(const IAgentRepository &repo, ConversationId conversation)
{
    QList<karness::Message> out;
    const QList<AgentMessageRecord> records = repo.messagesFor(conversation); // seq ascending
    out.reserve(records.size());
    for (const AgentMessageRecord &rec : records) {
        const QJsonDocument doc = QJsonDocument::fromJson(rec.contentJson.toUtf8());
        if (!doc.isObject())
            continue; // corrupt row — skip, never fatal
        const std::expected<karness::Message, karness::MessageCodecError> msg =
            karness::messageFromJson(doc.object());
        if (msg.has_value())
            out.append(*msg);
    }
    return out;
}

void appendAll(IAgentRepository &repo, ConversationId conversation,
               const QList<karness::Message> &messages, const Clock &clock)
{
    // Continue seq from the conversation's current tail so re-loads stay ordered.
    int nextSeq = 0;
    const QList<AgentMessageRecord> existing = repo.messagesFor(conversation);
    for (const AgentMessageRecord &rec : existing)
        nextSeq = std::max(nextSeq, rec.seq + 1);

    const QDateTime now = QDateTime::fromMSecsSinceEpoch(clock.nowMs(), QTimeZone::UTC);
    for (const karness::Message &msg : messages) {
        AgentMessageRecord rec;
        rec.id = AgentMessageId::generate();
        rec.conversation = conversation;
        rec.seq = nextSeq++;
        rec.role = static_cast<int>(msg.role);
        rec.contentJson =
            QString::fromUtf8(QJsonDocument(karness::messageToJson(msg)).toJson(QJsonDocument::Compact));
        rec.createdAt = now;
        repo.appendMessage(rec);
    }
}

} // namespace klr::transcript
