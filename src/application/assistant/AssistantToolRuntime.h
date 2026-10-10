#pragma once

#include "AssistantToolRegistry.h"
#include "AssistantApprovalCoordinator.h"
#include "../files/WorkspaceFileService.h"
#include "../notes/INotesRepository.h"
#include "../services/IServiceDefinitionRepository.h"
#include "../services/ServiceCoordinator.h"
#include "../tasks/TaskScheduler.h"
#include "../workspaces/WorkspaceContext.h"
#include "../workspaces/WorkspaceManager.h"
#include "../git/GitService.h"

namespace devhub::application::assistant {

// Application composition for scoped reads and Phase 9 approved mutations.
// Repositories and coordinators are called only on their owning thread; only
// immutable paths/value snapshots and existing service operations reach workers.
class AssistantToolRuntime final : public QObject
{
    Q_OBJECT
public:
    AssistantToolRuntime(ICodexClient& client, AssistantController& controller,
        tasks::TaskScheduler& scheduler, workspaces::WorkspaceManager& workspaces,
        workspaces::WorkspaceContext& workspaceContext,
        services::IServiceDefinitionRepository& serviceDefinitions,
        services::ServiceCoordinator& serviceCoordinator, notes::INotesRepository& notes,
        QObject* parent = nullptr);
    ~AssistantToolRuntime() override;

    [[nodiscard]] bool enabledForTestedWireProfile() const { return m_profileConfigured; }
    [[nodiscard]] QString registryFingerprint() const { return m_registryFingerprint; }
    [[nodiscard]] const AssistantToolRegistry& registry() const { return m_registry; }
    [[nodiscard]] static QJsonArray experimentalToolSpecifications();
    [[nodiscard]] static QString internalName(const QString& wireName);
    struct MutationIntegration final {
        std::function<bool(const QString&, const QString&)> noteEditable;
        std::function<shared::common::Result<QString>(const QString&, const QString&, const QString&, const QString&)> editNote;
        // Synchronous shared workflow: revalidate after transition prompts,
        // call committed after activation succeeds and before scope changes.
        std::function<bool(const QString&, std::function<bool()>, std::function<void()>)> openWorkspace;
    };
    void setMutationIntegration(MutationIntegration integration) { m_mutations = std::move(integration); }
    [[nodiscard]] AssistantApprovalCoordinator& approvals() { return m_approvals; }

private:
    void registerHandlers();
    void registerMutationHandlers();
    [[nodiscard]] bool scopeStillCurrent(const ToolCall& call) const;
    void executeServiceAction(const ToolCall&, const ToolCancellation&, QString action,
                              domain::services::ServiceDefinition, std::function<void(ToolResult)> done);
    void dispatch(const QJsonValue& requestId, const QJsonObject& parameters, quint64 processGeneration);
    [[nodiscard]] ToolScope scopeFor(const QJsonObject& parameters, quint64 processGeneration) const;
    void submitJsonTask(const ToolCall& call, const ToolCancellation& cancellation,
        std::function<shared::common::Result<QJsonObject>(const tasks::TaskContext&)> work,
        std::function<void(ToolResult)> done);
    void reportActivity(const QString& wireName, bool active, bool success = false);

    ICodexClient& m_client;
    AssistantController& m_controller;
    tasks::TaskScheduler& m_scheduler;
    workspaces::WorkspaceManager& m_workspaces;
    workspaces::WorkspaceContext& m_workspaceContext;
    services::IServiceDefinitionRepository& m_serviceDefinitions;
    services::ServiceCoordinator& m_serviceCoordinator;
    notes::INotesRepository& m_notes;
    AssistantToolRegistry m_registry;
    AssistantApprovalCoordinator m_approvals;
    MutationIntegration m_mutations;
    QString m_registryFingerprint;
    bool m_profileConfigured = false;
};

} // namespace devhub::application::assistant
