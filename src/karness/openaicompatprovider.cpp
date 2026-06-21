// SPDX-License-Identifier: GPL-3.0-or-later
#include "openaicompatprovider.h"

#include "chatcompletionsdialect.h"

namespace karness {

OpenAiCompatProvider::OpenAiCompatProvider(OpenAiCompatConfig config, QObject *parent)
    : StreamingProvider(std::move(config), std::make_unique<ChatCompletionsDialect>(), parent)
{
}

} // namespace karness
