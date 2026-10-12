#pragma once
#include "application/commands/CommandRegistry.h"
#include <utility>
namespace devhub::presentation::assistant {
struct AssistantNavigation final {
    static constexpr int Index = 9;
    static void registerCommand(application::commands::CommandRegistry& registry, std::function<void(int)> select)
    {
        registry.addCommand(QStringLiteral("nav.assistant"), QObject::tr("Assistant"),
            application::commands::CommandCategory::Navigation, [select = std::move(select)] { select(Index); }, {},
            QObject::tr("Open the Assistant module"), {QStringLiteral("assistant"), QStringLiteral("chat")});
    }
};
}
