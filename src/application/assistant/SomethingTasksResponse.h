#pragma once

#include "AssistantToolRegistry.h"

namespace devhub::application::assistant {

inline constexpr qsizetype kSomethingTasksFindOutputBytes = 48 * 1024;
inline constexpr qsizetype kSomethingTaskReadOutputBytes = 40 * 1024;

// Budget the complete tool envelope before publishing UI status or handing the
// response to the registry's last-resort output guard.
[[nodiscard]] ToolResult boundSomethingTasksResponse(ToolResult result, qsizetype maximumBytes);

} // namespace devhub::application::assistant
