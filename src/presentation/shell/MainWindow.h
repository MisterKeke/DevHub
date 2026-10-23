#pragma once
#include <functional>

#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QSet>
#include <QHash>
#include <optional>

#include "../../application/workspaces/WorkspaceManager.h"
#include "../../application/workspaces/WorkspaceContext.h"
#include "../../application/workspaces/WorkspaceStateStore.h"
#include "../../application/workspaces/WorkspaceRelocationService.h"
#include "../../application/workspaces/ITerminalLauncher.h"
#include "../../application/tasks/TaskScheduler.h"
#include "../../application/services/ServiceCoordinator.h"
#include "../../application/services/IServiceDefinitionRepository.h"
#include "../../application/events/ApplicationEventHub.h"
#include "../../application/notifications/INotificationRepository.h"
#include "../../application/notifications/NotificationService.h"
#include "../../shared/security/RedactionService.h"

class QResizeEvent;
class QCloseEvent;
class QLabel;
class QToolButton;
class QStackedWidget;
class QWidget;
class QDialog;
class QLineEdit;
class QListView;
namespace devhub::application::authentication {
class AuthenticationService;
class SessionController;
enum class SessionState;
}
namespace devhub::application::ports { class PortMonitorService; }
namespace devhub::application::logs { class ILogSourceRepository; }
namespace devhub::application::databases { class IDatabaseReferenceRepository; class ISqliteConnectionFactory; }
namespace devhub::application::notes { class INotesRepository; }
namespace devhub::application::files { class ITrashService; class IExternalEditorLauncher; }

namespace devhub::presentation::authentication {
class UnlockWidget;
}
namespace devhub::presentation::workspaces { class WorkspaceListModel; }
namespace devhub::presentation::widgets { class CurrentPageStack; }
namespace devhub::presentation::notes { class NotesPage; }
namespace devhub::presentation::assistant { class AssistantPage; }
namespace devhub::application::assistant { class AssistantController; class AssistantToolRuntime; class ICodexExecutableProbe; }
namespace devhub::presentation::databases { class DatabasePage; }
namespace devhub::presentation::services { class ServicePage; }
namespace devhub::presentation::logs { class LogsPage; }
namespace devhub::application::overview { class OverviewStateComposer; }
namespace devhub::application::commands { class CommandRegistry; }
namespace devhub::application::commands { class CachedSearchProvider; }
namespace devhub::application::settings { class SettingsService; }
namespace devhub::application::planner { class PlannerService; }
namespace devhub::application::lifecycle { class ShutdownCoordinator; }
namespace devhub::application::support { class SupportBundleBuilder; }
namespace devhub::presentation::theme { class ThemeManager; }
namespace devhub::presentation::overview { class OverviewPage; }
namespace devhub::presentation::overview::planner { class PlannerController; }
namespace devhub::presentation::overview::planner { class PlannerImportController; }
namespace devhub::presentation::commands { class CommandPaletteDialog; }
namespace devhub::presentation::notifications { class ToastManager; }

namespace devhub::presentation::shell {

class MainWindow final : public QMainWindow
{
    Q_OBJECT
    Q_PROPERTY(QString shellWorkspaceTitle READ shellWorkspaceTitle NOTIFY shellChanged)
    Q_PROPERTY(QString shellWorkspacePath READ shellWorkspacePath NOTIFY shellChanged)
    Q_PROPERTY(QString shellWorkspaceId READ shellWorkspaceId NOTIFY shellChanged)
    Q_PROPERTY(QVariantList overviewSections READ overviewSections NOTIFY shellChanged)
    Q_PROPERTY(int quickModule READ quickModule NOTIFY shellChanged)
    Q_PROPERTY(bool workspaceRemovalInProgress READ workspaceRemovalInProgress NOTIFY shellChanged)
    Q_PROPERTY(QString workspaceActionMessage READ workspaceActionMessage NOTIFY shellChanged)
    Q_PROPERTY(QString workspaceRemovalWorkspaceId READ workspaceRemovalWorkspaceId NOTIFY shellChanged)
    Q_PROPERTY(QString workspaceActionTone READ workspaceActionTone NOTIFY shellChanged)
public:
    MainWindow(application::authentication::SessionController& session,
               application::authentication::AuthenticationService& authentication,
               application::workspaces::WorkspaceManager& workspaceManager,
               application::workspaces::WorkspaceContext& workspaceContext,
               application::workspaces::WorkspaceStateStore& workspaceState,
               application::workspaces::WorkspaceRelocationService& relocation,
               application::workspaces::ITerminalLauncher& terminalLauncher,
               application::tasks::TaskScheduler& scheduler,
               application::services::IServiceDefinitionRepository& serviceRepository,
               application::services::ServiceCoordinator& serviceCoordinator,
               application::ports::PortMonitorService& portMonitor,
               application::logs::ILogSourceRepository& logSourceRepository,
               application::databases::IDatabaseReferenceRepository& databaseReferences,
               application::databases::ISqliteConnectionFactory& databaseFactory,
               application::notes::INotesRepository& notesRepository,
               QString notesStorageRoot,
               QString accountId,
               application::files::ITrashService& trashService,
               application::files::IExternalEditorLauncher& editorLauncher,
               application::settings::SettingsService& settingsService,
               application::planner::PlannerService& plannerService,
               presentation::theme::ThemeManager& themeManager,
               application::events::ApplicationEventHub& eventHub,
               application::notifications::INotificationRepository& notificationRepository,
               application::notifications::NotificationService& notificationService,
               application::support::SupportBundleBuilder& supportBundleBuilder,
               const shared::security::RedactionService& redactor,
               application::assistant::AssistantController& assistantController,
               application::assistant::AssistantToolRuntime& assistantToolRuntime,
               application::assistant::ICodexExecutableProbe& assistantSetupProbe,
               application::lifecycle::ShutdownCoordinator& shutdownCoordinator,
               QWidget* parent = nullptr);
    void restoreWorkspaceSelection();
    bool flushNotesForShutdown();
    [[nodiscard]] bool confirmDatabaseTransition();
    [[nodiscard]] bool requestAssistantWorkspaceOpen(const QString& id,
        std::function<bool()> revalidate, std::function<void()> committed);
    void showRecoveryNotice();
    QString shellWorkspaceTitle() const;
    QString shellWorkspacePath() const;
    QString shellWorkspaceId() const;
    QVariantList overviewSections() const;
    int quickModule() const noexcept { return m_quickModule; }
    bool workspaceRemovalInProgress() const noexcept { return !m_archivingWorkspaceIds.isEmpty(); }
    QString workspaceActionMessage() const { return m_workspaceActionMessage; }
    QString workspaceRemovalWorkspaceId() const { return m_removalWorkspaceId; }
    QString workspaceActionTone() const { return m_workspaceActionTone; }

public slots:
    void selectQuickModule(int index);
    void activateQuickWorkspace(const QString& id);
    void addQuickWorkspace();
    void lockQuickSession();
    void openQuickTerminal();
    void restartQuickServices();
    void openQuickFolder();
    void openQuickSettings();
    void showQuickWorkspaceMenu(const QString& id);
    void manageArchivedWorkspaces();
    void cancelQuickWorkspaceRemoval(const QString& id);

signals:
    void manualLockRequested();
    void signOutRequested();
    void quitRequested();
    void shellChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void restoreSafeGeometry();
    void updateHeaderForWidth();
    void selectPage(int index);
    void updateSessionPage(application::authentication::SessionState state);
    void submitUnlock();
    void reloadWorkspaces(QString preferredId = {}, bool chooseFallback = true);
    void addWorkspace(QString initialPath = {});
    bool activateWorkspace(const QString& id, std::function<bool()> revalidate = {},
                           std::function<void()> committed = {});
    void showWorkspaceMenu(const QPoint& position);
    void showWorkspaceMenuForId(const QString& id, const QPoint& globalPosition);
    void archiveWorkspace(const QString& id);
    void finishWorkspaceArchive(const QString& id, const QString& name, bool serviceStopTimedOut = false);
    void restoreWorkspaceAfterArchiveFailure(const QString& id);
    void refreshPlannerServiceOptions(const QString& workspaceId);
    [[nodiscard]] bool plannerWorkspaceAuthorized(const QString& workspaceId) const;
    void cancelPendingPlannerServiceStarts(const QString& workspaceId = {});
    void handlePlannerServiceState(const QString& serviceId,
                                   const application::services::ServiceRuntimeState& state);

    QLabel* m_workspaceTitle = nullptr;
    QLabel* m_workspacePath = nullptr;
    devhub::presentation::widgets::CurrentPageStack* m_pageHost = nullptr;
    QToolButton* m_terminalButton = nullptr;
    QToolButton* m_restartButton = nullptr;
    QToolButton* m_folderButton = nullptr;
    QToolButton* m_settingsButton = nullptr;
    QToolButton* m_notificationsButton = nullptr;
    QToolButton* m_overflowButton = nullptr;
    QWidget* m_headerActions = nullptr;
    application::authentication::SessionController& m_session;
    application::authentication::AuthenticationService& m_authentication;
    application::workspaces::WorkspaceManager& m_workspaceManager;
    application::workspaces::WorkspaceContext& m_workspaceContext;
    application::workspaces::WorkspaceStateStore& m_workspaceState;
    application::workspaces::WorkspaceRelocationService& m_relocation;
    application::workspaces::ITerminalLauncher& m_terminalLauncher;
    application::tasks::TaskScheduler& m_scheduler;
    application::services::IServiceDefinitionRepository& m_serviceRepository;
    application::services::ServiceCoordinator& m_serviceCoordinator;
    application::settings::SettingsService& m_settingsService;
    application::assistant::AssistantController& m_assistantController;
    application::assistant::AssistantToolRuntime& m_assistantToolRuntime;
    application::assistant::ICodexExecutableProbe& m_assistantSetupProbe;
    application::lifecycle::ShutdownCoordinator& m_shutdownCoordinator;
    presentation::theme::ThemeManager& m_themeManager;
    application::events::ApplicationEventHub& m_eventHub;
    application::notifications::INotificationRepository& m_notificationRepository;
    application::notifications::NotificationService& m_notificationService;
    application::support::SupportBundleBuilder& m_supportBundleBuilder;
    const shared::security::RedactionService& m_redactor;
    devhub::presentation::notifications::ToastManager* m_toastManager = nullptr;
    bool m_closeAfterServicesStop = false;
    bool m_shutdownRequested = false;
    devhub::presentation::workspaces::WorkspaceListModel* m_workspaceModel = nullptr;
    QListView* m_workspaceList = nullptr;
    QLineEdit* m_workspaceSearch = nullptr;
    QStackedWidget* m_rootPages = nullptr;
    QWidget* m_mainShell = nullptr;
    QWidget* m_quickShell = nullptr;
    QWidget* m_lockPage = nullptr;
    authentication::UnlockWidget* m_unlockWidget = nullptr;
    devhub::presentation::notes::NotesPage* m_notesPage = nullptr;
    devhub::presentation::assistant::AssistantPage* m_assistantPage = nullptr;
    devhub::presentation::databases::DatabasePage* m_databasePage = nullptr;
    devhub::presentation::services::ServicePage* m_servicePage = nullptr;
    devhub::presentation::logs::LogsPage* m_logsPage = nullptr;
    application::overview::OverviewStateComposer* m_overviewComposer = nullptr;
    application::commands::CommandRegistry* m_commandRegistry = nullptr;
    devhub::presentation::overview::OverviewPage* m_overviewPage = nullptr;
    devhub::presentation::overview::planner::PlannerController* m_plannerController = nullptr;
    devhub::presentation::overview::planner::PlannerImportController* m_plannerImportController = nullptr;
    devhub::presentation::commands::CommandPaletteDialog* m_commandPalette = nullptr;
    application::commands::CachedSearchProvider* m_workspaceSearchProvider = nullptr;
    application::commands::CachedSearchProvider* m_plannerSearchProvider = nullptr;
    application::commands::CachedSearchProvider* m_servicesSearchProvider = nullptr;
    application::commands::CachedSearchProvider* m_notesSearchProvider = nullptr;
    application::commands::CachedSearchProvider* m_databaseSearchProvider = nullptr;
    application::commands::CachedSearchProvider* m_settingsSearchProvider = nullptr;
    qint64 m_observedLogBytes = 0;
    QStringList m_recentLogLines;
    QPointer<QWidget> m_focusBeforeLock;
    QDialog* m_moduleWindow = nullptr;
    QList<QWidget*> m_modulePages;
    int m_quickModule = 0;
    QSet<QString> m_archivingWorkspaceIds;
    QString m_workspaceActionMessage;
    QString m_workspaceActionTone = QStringLiteral("info");
    QString m_removalWorkspaceId;
    QStringList m_workspaceCommandIds;
    std::optional<devhub::domain::workspace::Workspace> m_workspaceBeingArchivedActive;
    struct PendingPlannerServiceStart final {
        QString workspaceId;
        QString taskId;
        QString serviceId;
        QString runId;
        QString serviceName;
        quint64 actionIdentity = 0;
    };
    QHash<QString, PendingPlannerServiceStart> m_pendingPlannerServiceStarts;
};

} // namespace devhub::presentation::shell
