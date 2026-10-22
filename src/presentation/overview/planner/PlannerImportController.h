#pragma once

#include "../../../application/tasks/TaskScheduler.h"
#include "../../../domain/planner/PlannerTypes.h"
#include "../../../infrastructure/planner/SomethingQtImportReader.h"

#include <QObject>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <algorithm>
#include <limits>
#include <optional>

namespace devhub::application::assistant { class AssistantToolRuntime; }
namespace devhub::application::authentication { class SessionController; }
namespace devhub::application::planner { class PlannerService; }
namespace devhub::application::settings { class SettingsService; }
namespace devhub::application::workspaces { class WorkspaceContext; class WorkspaceManager; }

namespace devhub::presentation::overview::planner {

class PlannerImportController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString profilePath READ profilePath NOTIFY changed)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY changed)
    Q_PROPERTY(bool verified READ verified NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool importing READ importing NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString resultMessage READ resultMessage NOTIFY changed)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY changed)
    Q_PROPERTY(QStringList sourceTags READ sourceTags NOTIFY changed)
    Q_PROPERTY(QVariantList workspaceOptions READ workspaceOptions NOTIFY changed)
    Q_PROPERTY(QString destinationWorkspaceId READ destinationWorkspaceId NOTIFY changed)
    Q_PROPERTY(QString destinationWorkspaceName READ destinationWorkspaceName NOTIFY changed)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY changed)
    Q_PROPERTY(qint64 totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(int page READ page NOTIFY changed)
    Q_PROPERTY(int pageSize READ pageSize CONSTANT)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(qint64 expectedCount READ expectedCount NOTIFY changed)
    Q_PROPERTY(bool confirmationReady READ confirmationReady NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString progressMessage READ progressMessage NOTIFY changed)
    Q_PROPERTY(QString searchText READ searchText NOTIFY changed)
    Q_PROPERTY(QString tagFilter READ tagFilter NOTIFY changed)
    Q_PROPERTY(QString priorityFilter READ priorityFilter NOTIFY changed)
    Q_PROPERTY(int completionFilter READ completionFilter NOTIFY changed)
public:
    PlannerImportController(application::planner::PlannerService& planner,
        application::workspaces::WorkspaceManager& workspaces,
        application::workspaces::WorkspaceContext& workspaceContext,
        application::tasks::TaskScheduler& scheduler,
        application::assistant::AssistantToolRuntime& assistantRuntime,
        application::settings::SettingsService& settings,
        application::authentication::SessionController& session,
        QObject* parent = nullptr);
    ~PlannerImportController() override;

    QString profilePath() const { return m_profilePath; }
    QString connectionStatus() const { return m_connectionStatus; }
    bool verified() const { return m_verified; }
    bool loading() const { return m_loading; }
    bool importing() const { return m_importing; }
    QString error() const { return m_error; }
    QString resultMessage() const { return m_resultMessage; }
    QVariantList tasks() const { return m_tasks; }
    QStringList sourceTags() const { return m_sourceTags; }
    QVariantList workspaceOptions() const { return m_workspaceOptions; }
    QString destinationWorkspaceId() const { return m_destinationWorkspaceId; }
    QString destinationWorkspaceName() const { return m_destinationWorkspaceName; }
    QVariantMap preview() const { return m_preview; }
    qint64 totalCount() const { return m_totalCount; }
    int page() const { return m_page; }
    int pageSize() const { return 50; }
    int selectedCount() const { return static_cast<int>(std::min<qsizetype>(m_selectedIds.size(), std::numeric_limits<int>::max())); }
    qint64 expectedCount() const { return m_expectedCount; }
    bool confirmationReady() const { return m_confirmationReady; }
    int progress() const { return m_progress; }
    QString progressMessage() const { return m_progressMessage; }
    QString searchText() const { return m_searchText; }
    QString tagFilter() const { return m_tagFilter; }
    QString priorityFilter() const { return m_priorityFilter; }
    int completionFilter() const { return m_completionFilter; }

    Q_INVOKABLE void openDialog();
    Q_INVOKABLE void browseProfile();
    Q_INVOKABLE void setProfilePath(const QString& path);
    Q_INVOKABLE void verifyConnection();
    Q_INVOKABLE void setSearchText(const QString& text);
    Q_INVOKABLE void setTagFilter(const QString& tag);
    Q_INVOKABLE void setPriorityFilter(const QString& priority);
    Q_INVOKABLE void setCompletionFilter(int completion);
    Q_INVOKABLE void previousPage();
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void selectTask(const QString& sourceTaskId, bool selected);
    Q_INVOKABLE void selectAllVisible();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void previewTask(const QString& sourceTaskId);
    Q_INVOKABLE void setDestinationWorkspaceId(const QString& workspaceId);
    Q_INVOKABLE void toggleImportTag(const QString& tag, bool selected);
    Q_INVOKABLE bool isImportTagSelected(const QString& tag) const;
    Q_INVOKABLE void prepareImport(const QString& mode);
    Q_INVOKABLE void startImport(bool allowAdditionalCopies);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE void closeDialog();
    void setSessionAllowed(bool allowed);

signals:
    void changed();
    void confirmationReady(qint64 expectedCount);
    void tasksImported();

private:
    void schedulePageLoad(bool resetPage = true);
    void loadPage();
    void reloadWorkspaces();
    void invalidateSource(QString status);
    bool destinationStillValid(const QString& workspaceId) const;
    bool commitOne(domain::planner::PlannerTask task, const QString& workspaceId,
                   bool allowAdditionalCopies,
                   const application::tasks::CancellationToken& cancellation);
    void setFailure(QString message);
    infrastructure::planner::SomethingQtImportSelection selectionForMode(const QString& mode) const;
    static QVariantMap summaryMap(const infrastructure::planner::SomethingQtImportSummary& summary,
                                  bool selected, int importCount);
    static QVariantMap previewMap(const domain::planner::PlannerTask& task);

    application::planner::PlannerService& m_planner;
    application::workspaces::WorkspaceManager& m_workspaces;
    application::workspaces::WorkspaceContext& m_workspaceContext;
    application::tasks::TaskScheduler& m_scheduler;
    application::assistant::AssistantToolRuntime& m_assistantRuntime;
    application::settings::SettingsService& m_settings;
    application::authentication::SessionController& m_session;
    QString m_profilePath;
    QString m_profileIdentity;
    QString m_connectionStatus = QStringLiteral("Not verified");
    QString m_error;
    QString m_resultMessage;
    QString m_destinationWorkspaceId;
    QString m_destinationWorkspaceName;
    QString m_searchText;
    QString m_tagFilter;
    QString m_priorityFilter;
    int m_completionFilter = -1;
    int m_page = 0;
    qint64 m_totalCount = 0;
    qint64 m_expectedCount = 0;
    int m_progress = 0;
    QString m_progressMessage;
    QString m_importMode;
    QVariantList m_tasks;
    QStringList m_sourceTags;
    QVariantList m_workspaceOptions;
    QVariantMap m_preview;
    QSet<QString> m_selectedIds;
    QSet<QString> m_importTags;
    QHash<QString, int> m_importCounts;
    std::optional<application::tasks::TaskHandle> m_verifyHandle;
    std::optional<application::tasks::TaskHandle> m_pageHandle;
    std::optional<application::tasks::TaskHandle> m_previewHandle;
    std::optional<application::tasks::TaskHandle> m_prepareHandle;
    std::optional<application::tasks::TaskHandle> m_importHandle;
    QTimer m_filterTimer;
    quint64 m_verifyGeneration = 0;
    quint64 m_pageGeneration = 0;
    quint64 m_previewGeneration = 0;
    quint64 m_prepareGeneration = 0;
    quint64 m_importGeneration = 0;
    qint64 m_importedCount = 0;
    qint64 m_skippedCount = 0;
    bool m_verified = false;
    bool m_loading = false;
    bool m_importing = false;
    bool m_preparing = false;
    bool m_confirmationReady = false;
    bool m_sessionAllowed = true;
};

} // namespace devhub::presentation::overview::planner
