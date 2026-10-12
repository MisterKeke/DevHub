#include "AssistantToolHandlers.h"

#include <QUuid>

namespace devhub::application::assistant {

bool registerBoundedNoteReadTool(AssistantToolRegistry& registry, application::notes::INotesRepository& notes)
{
    ToolDefinition definition;
    definition.name = QStringLiteral("notes_read");
    definition.description = QStringLiteral("Read one authorized note with a bounded body.");
    definition.allowedArguments = {QStringLiteral("noteId")};
    definition.requiredArguments = definition.allowedArguments;
    definition.affinity = ToolAffinity::OwnerThread;
    definition.maximumOutputBytes = 16 * 1024;
    definition.requiresWorkspace = true;
    definition.requiresNoteReadDisclosure = true;
    definition.validateArguments = [](const QJsonObject& input, QJsonObject* normalized) {
        const QString id = input.value(QStringLiteral("noteId")).toString();
        if (input.size() != 1 || QUuid::fromString(id).isNull() || id.size() > 64) return false;
        *normalized = {{QStringLiteral("noteId"), id}};
        return true;
    };
    return registry.registerTool(std::move(definition), [&notes](const ToolCall& call, const ToolCancellation&, auto complete) {
        // This adapter runs on the registry/repository owner thread. The repo
        // performs account/workspace/non-trash ownership checks before I/O.
        const QString noteId = call.arguments.value(QStringLiteral("noteId")).toString();
        if (!call.scope.allowsNoteRead(noteId)) {
            ToolResult denied; denied.errorCode = QStringLiteral("disclosure_denied");
            denied.data.insert(QStringLiteral("error"), denied.errorCode); complete(std::move(denied)); return;
        }
        const auto content = notes.readContentBounded(call.scope.workspaceId(), noteId, 2 * 1024 * 1024);
        ToolResult result;
        if (!content) {
            result.errorCode = QStringLiteral("note_unavailable");
            result.data.insert(QStringLiteral("error"), result.errorCode);
        } else {
            result.success = true;
            result.data.insert(QStringLiteral("text"), content.value());
        }
        complete(std::move(result));
    });
}

} // namespace devhub::application::assistant
