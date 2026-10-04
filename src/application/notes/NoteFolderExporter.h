#pragma once

#include "INotesRepository.h"

namespace devhub::application::notes
{

// Validates every destination before creating the export root. Existing
// destinations and collisions under Windows filename rules are refused.
[[nodiscard]] shared::common::Result<QString> exportNoteFolder(INotesRepository &repository,
                                                               const QString &workspaceId,
                                                               const QString &folderId,
                                                               const QString &destination);

} // namespace devhub::application::notes
