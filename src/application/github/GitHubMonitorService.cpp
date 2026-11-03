#include "GitHubMonitorService.h"

#include "GitHubService.h"
#include "../../application/notifications/Notification.h"
#include "../../application/notifications/NotificationService.h"
#include "../../application/settings/SettingsService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace devhub::application::github {
namespace {
constexpr auto MonitorSnapshotSetting = "github/monitorSnapshot";

QJsonObject pullRequestSnapshot(const GitHubPullRequest& pullRequest)
{
    return {{QStringLiteral("state"), pullRequest.state},
            {QStringLiteral("merged"), pullRequest.merged},
            {QStringLiteral("updated"), pullRequest.updatedAtUtc.toUTC().toString(Qt::ISODateWithMs)}};
}

QJsonObject workflowRunSnapshot(const GitHubWorkflowRun& run)
{
    return {{QStringLiteral("status"), run.status},
            {QStringLiteral("conclusion"), run.conclusion},
            {QStringLiteral("updated"), run.completedAtUtc.toUTC().toString(Qt::ISODateWithMs)}};
}
}

GitHubMonitorService::GitHubMonitorService(GitHubService& github, settings::SettingsService& settings,
                                           notifications::NotificationService& notifications, QObject* parent)
    : QObject(parent), m_github(github), m_settings(settings), m_notifications(notifications)
{
    m_timer.setSingleShot(false);
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &GitHubMonitorService::poll);
    connect(&m_github, &GitHubService::connectionChanged, this, [this] { updateSchedule(); });
    connect(&m_github, &GitHubService::monitorSnapshotLoaded, this, &GitHubMonitorService::processSnapshot);
}

void GitHubMonitorService::setTarget(QString workspaceId, GitHubRepository repository)
{
    if (m_workspaceId == workspaceId && m_repository == repository) return;
    ++m_targetGeneration;
    m_github.cancelMonitoringRequests();
    m_inFlight = false;
    m_workspaceId = std::move(workspaceId);
    m_repository = std::move(repository);
    updateSchedule();
}

void GitHubMonitorService::clearTarget()
{
    if (m_workspaceId.isEmpty() && !m_repository) return;
    ++m_targetGeneration;
    m_github.cancelMonitoringRequests();
    m_inFlight = false;
    m_workspaceId.clear();
    m_repository.reset();
    m_timer.stop();
}

void GitHubMonitorService::configurationChanged()
{
    updateSchedule();
}

void GitHubMonitorService::updateSchedule()
{
    m_timer.stop();
    if (m_workspaceId.isEmpty() || !m_repository
        || m_github.connection().state != GitHubConnection::State::Connected
        || !m_settings.value(QStringLiteral("github/monitoringEnabled"), m_workspaceId).toBool()) {
        if (m_inFlight) {
            ++m_targetGeneration;
            m_github.cancelMonitoringRequests();
            m_inFlight = false;
        }
        return;
    }
    const int seconds = qBound(60,
        m_settings.value(QStringLiteral("github/refreshIntervalSeconds"), m_workspaceId).toInt(), 3600);
    m_timer.start(seconds * 1000);
    if (!m_inFlight) QTimer::singleShot(0, this, &GitHubMonitorService::poll);
}

void GitHubMonitorService::poll()
{
    if (m_inFlight || !m_repository || m_workspaceId.isEmpty()
        || m_github.connection().state != GitHubConnection::State::Connected
        || !m_settings.value(QStringLiteral("github/monitoringEnabled"), m_workspaceId).toBool()) return;
    m_inFlight = true;
    m_github.loadMonitorSnapshot(*m_repository);
}

void GitHubMonitorService::processSnapshot(GitHubMonitorSnapshot snapshot, QString error)
{
    if (!m_inFlight) return;
    m_inFlight = false;
    const quint64 generation = m_targetGeneration;
    if (!error.isEmpty() || !m_repository || m_workspaceId.isEmpty()
        || m_github.connection().state != GitHubConnection::State::Connected
        || !m_settings.value(QStringLiteral("github/monitoringEnabled"), m_workspaceId).toBool()) return;

    QJsonObject previous;
    const QString savedText = m_settings.value(QString::fromLatin1(MonitorSnapshotSetting), m_workspaceId).toString();
    if (!savedText.isEmpty()) {
        QJsonParseError parseError;
        const auto saved = QJsonDocument::fromJson(savedText.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && saved.isObject()) previous = saved.object();
    }
    const bool sameRepository = previous.value(QStringLiteral("repository")).toString() == m_repository->fullName();
    const bool hasBaseline = sameRepository && previous.value(QStringLiteral("schema")).toInt() == 1;
    const QJsonObject oldPullRequests = hasBaseline
        ? previous.value(QStringLiteral("pullRequests")).toObject() : QJsonObject{};
    const QJsonObject oldWorkflowRuns = hasBaseline
        ? previous.value(QStringLiteral("workflowRuns")).toObject() : QJsonObject{};
    QJsonObject currentPullRequests;
    QJsonObject currentWorkflowRuns;
    QVector<notifications::NotificationDraft> drafts;
    const QString repositoryName = m_repository->fullName();
    const auto workspace = QUuid::fromString(m_workspaceId);

    for (const auto& pullRequest : snapshot.pullRequests) {
        const QString id = QString::number(pullRequest.number);
        const QJsonObject state = pullRequestSnapshot(pullRequest);
        currentPullRequests.insert(id, state);
        const auto old = oldPullRequests.value(id).toObject();
        if (hasBaseline && !old.value(QStringLiteral("merged")).toBool() && pullRequest.merged) {
            notifications::NotificationDraft draft;
            draft.severity = notifications::NotificationSeverity::Success;
            draft.category = notifications::NotificationCategory::GitHub;
            draft.title = tr("Pull Request #%1 merged").arg(pullRequest.number);
            draft.message = tr("%1 in %2 was merged.").arg(pullRequest.title.left(240), repositoryName);
            draft.deduplicationKey = QStringLiteral("github:%1:%2:merged:%3")
                .arg(m_workspaceId, id, pullRequest.updatedAtUtc.toUTC().toString(Qt::ISODateWithMs));
            draft.workspaceId = workspace.isNull() ? std::optional<QUuid>{} : std::optional<QUuid>{workspace};
            drafts.push_back(std::move(draft));
        }
    }
    for (const auto& run : snapshot.workflowRuns) {
        const QString id = QString::number(run.id);
        currentWorkflowRuns.insert(id, workflowRunSnapshot(run));
        const auto old = oldWorkflowRuns.value(id).toObject();
        if (hasBaseline && run.status == QStringLiteral("completed")
            && old.value(QStringLiteral("status")).toString() != QStringLiteral("completed")) {
            notifications::NotificationDraft draft;
            draft.severity = run.conclusion == QStringLiteral("failure")
                ? notifications::NotificationSeverity::Warning : notifications::NotificationSeverity::Information;
            draft.category = notifications::NotificationCategory::GitHub;
            draft.title = run.conclusion == QStringLiteral("failure")
                ? tr("GitHub Actions failed") : tr("GitHub Actions completed");
            draft.message = tr("%1 in %2: %3 (%4).")
                .arg(run.name.left(180), repositoryName,
                     run.conclusion.isEmpty() ? tr("no conclusion") : run.conclusion,
                     run.headSha.left(12));
            draft.deduplicationKey = QStringLiteral("github:%1:%2:completed:%3")
                .arg(m_workspaceId, id, run.completedAtUtc.toUTC().toString(Qt::ISODateWithMs));
            draft.workspaceId = workspace.isNull() ? std::optional<QUuid>{} : std::optional<QUuid>{workspace};
            drafts.push_back(std::move(draft));
        }
    }

    const QJsonObject current{{QStringLiteral("schema"), 1},
        {QStringLiteral("repository"), repositoryName},
        {QStringLiteral("pullRequests"), currentPullRequests},
        {QStringLiteral("workflowRuns"), currentWorkflowRuns}};
    const auto saved = m_settings.setValue(QString::fromLatin1(MonitorSnapshotSetting),
        QString::fromUtf8(QJsonDocument(current).toJson(QJsonDocument::Compact)), m_workspaceId);
    if (!saved || generation != m_targetGeneration) return;
    for (auto& draft : drafts) (void)m_notifications.post(std::move(draft));
}

} // namespace devhub::application::github
