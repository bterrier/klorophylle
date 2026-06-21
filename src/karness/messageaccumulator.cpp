// SPDX-License-Identifier: GPL-3.0-or-later
#include "messageaccumulator.h"

#include <QtCore/QJsonDocument>

namespace karness {

void MessageAccumulator::feed(const StreamEvent &event)
{
    std::visit(
        [this](const auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, TextDelta>) {
                if (!m_blocks.isEmpty() && std::holds_alternative<TextBlock>(m_blocks.last()))
                    std::get<TextBlock>(m_blocks.last()).text += e.text;
                else
                    m_blocks.append(TextBlock{e.text});
            } else if constexpr (std::is_same_v<T, ReasoningDelta>) {
                ReasoningBlock *block = nullptr;
                if (!m_blocks.isEmpty() && std::holds_alternative<ReasoningBlock>(m_blocks.last())) {
                    block = &std::get<ReasoningBlock>(m_blocks.last());
                } else if (e.providerOpaque && e.text.isEmpty()) {
                    // A trailing opaque-only frame (a signature / encrypted item
                    // arriving after the reasoning block closed): attach to the
                    // most recent reasoning block instead of opening a new one.
                    for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it)
                        if (auto *r = std::get_if<ReasoningBlock>(&*it)) {
                            block = r;
                            break;
                        }
                }
                if (!block) {
                    m_blocks.append(ReasoningBlock{e.text, {}});
                    block = &std::get<ReasoningBlock>(m_blocks.last());
                } else {
                    block->text += e.text;
                }
                if (e.providerOpaque) // dialect echo blob -> merged onto the block
                    for (auto it = e.providerOpaque->begin(); it != e.providerOpaque->end(); ++it)
                        block->providerOpaque.insert(it.key(), it.value());
            } else if constexpr (std::is_same_v<T, ToolCallStart>) {
                if (!m_calls.contains(e.index))
                    m_callOrder.append(e.index);
                m_calls.insert(e.index, PendingCall{e.id, e.name, {}});
            } else if constexpr (std::is_same_v<T, ToolCallArgsDelta>) {
                const auto it = m_calls.find(e.index);
                if (it != m_calls.end())
                    it->args += e.argsDelta;
            } else {
                // Done / ErrorEvent: terminal handling is the dialect's job.
                static_assert(std::is_same_v<T, Done> || std::is_same_v<T, ErrorEvent>);
            }
        },
        event);
}

std::expected<Message, AgentError> MessageAccumulator::finish() const
{
    Message message{Role::Assistant, m_blocks};
    // Tool calls close the message, in stream-arrival order.
    for (int index : m_callOrder) {
        const PendingCall &call = m_calls.value(index);
        QJsonObject args;
        const QByteArray raw = call.args.trimmed().toUtf8();
        if (!raw.isEmpty()) { // providers send "" for no-arg tools -> {}
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject())
                return std::unexpected(AgentError{
                    AgentError::Code::Parse,
                    QStringLiteral("malformed tool-call args for '%1': %2")
                        .arg(call.name, parseError.errorString()),
                    {}});
            args = doc.object();
        }
        message.blocks.append(ToolCallBlock{call.id, call.name, args});
    }
    return message;
}

} // namespace karness
