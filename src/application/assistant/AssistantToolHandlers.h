#pragma once

#include "AssistantToolRegistry.h"
#include "../notes/INotesRepository.h"

namespace devhub::application::assistant {

// Internal Phase 7 example handler. It is registered only by explicit DI and
// is never advertised to Codex in this phase.
[[nodiscard]] bool registerBoundedNoteReadTool(AssistantToolRegistry& registry,
    application::notes::INotesRepository& notes);

} // namespace devhub::application::assistant
