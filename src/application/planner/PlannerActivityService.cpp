#include "PlannerActivityService.h"

#include "../../domain/errors/AppError.h"
#include <QUuid>
#include <QRegularExpression>

namespace devhub::application::planner
{
using namespace domain::planner;
using shared::common::Result;

Result<QVector<PlannerActivityEvent>> PlannerActivityService::listEvents(
    const QString& workspaceId, const QString& taskId, int limit, int offset)
{
    return m_repository.listEvents(workspaceId, taskId, qBound(1, limit, 100), qMax(0, offset));
}

Result<QVector<PlannerGitCommitAssociation>> PlannerActivityService::listGitCommits(
    const QString& workspaceId, const QString& taskId)
{
    return m_repository.listGitCommits(workspaceId, taskId);
}

Result<void> PlannerActivityService::linkGitCommit(
    const QString& workspaceId, const QString& taskId, const application::git::GitCommit& commit,
    const QString& source, const QString& branchName)
{
    static const QRegularExpression shaPattern(QStringLiteral("^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$"));
    if (!shaPattern.match(commit.objectId).hasMatch()
        || (source != QStringLiteral("manual") && source != QStringLiteral("branch")
            && source != QStringLiteral("message"))) {
        return Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("Choose a valid Git commit and association source."), commit.objectId,
            QStringLiteral("link Git commit to Planner task")));
    }
    PlannerGitCommitAssociation association;
    association.workspaceId = workspaceId;
    association.taskId = taskId;
    association.commitSha = commit.objectId.toLower();
    association.associationSource = source;
    association.branchName = branchName;
    association.subject = commit.subject.left(512);
    association.author = commit.author.left(256);
    association.authoredAtUtc = commit.authoredAt.toUTC();
    association.committedAtUtc = commit.committedAt.toUTC();
    association.observedAtUtc = m_clock.utcNow().toUTC();
    return m_repository.linkGitCommit(std::move(association));
}

Result<void> PlannerActivityService::recordEvent(PlannerActivityEvent event)
{
    if (event.workspaceId.trimmed().isEmpty() || event.taskId.trimmed().isEmpty()
        || event.eventType.trimmed().isEmpty() || event.origin.trimmed().isEmpty()
        || event.description.trimmed().isEmpty() || event.metadataJson.toUtf8().size() > 16384
        || event.externalRefType.size() > 64 || event.externalRefId.size() > 128) {
        return Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("The Planner activity event is invalid."), {},
            QStringLiteral("record Planner activity event")));
    }
    if (event.id.isEmpty()) event.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDateTime now = m_clock.utcNow().toUTC();
    if (!event.occurredAtUtc.isValid()) event.occurredAtUtc = now;
    event.observedAtUtc = now;
    event.eventType = event.eventType.left(100);
    event.origin = event.origin.left(64);
    event.description = event.description.left(512);
    return m_repository.recordEvent(std::move(event));
}

Result<void> PlannerActivityService::unlinkGitCommit(
    const QString& workspaceId, const QString& taskId, const QString& sha)
{
    return m_repository.unlinkGitCommit(workspaceId, taskId, sha.toLower());
}

Result<void> PlannerActivityService::setGitCommitAvailability(
    const QString& workspaceId, const QString& taskId, const QString& sha, const QString& availability)
{
    if (availability != QStringLiteral("reachable") && availability != QStringLiteral("unavailable"))
        return Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("Choose a valid Git commit availability state."), availability,
            QStringLiteral("update Git commit availability")));
    return m_repository.setGitCommitAvailability(workspaceId, taskId, sha.toLower(), availability,
                                                  m_clock.utcNow().toUTC());
}

QString PlannerActivityService::taskReference(const QString& taskId)
{
    QString compact = taskId;
    compact.remove(QLatin1Char('-'));
    return QStringLiteral("DEVHUB-%1").arg(compact.toUpper());
}

bool PlannerActivityService::messageReferencesTask(const QString& message, const QString& taskId)
{
    const QString reference = taskReference(taskId);
    if (reference == QStringLiteral("DEVHUB-")) return false;
    const QRegularExpression pattern(QStringLiteral("(?i)(?<![A-Z0-9])%1(?![A-Z0-9])")
        .arg(QRegularExpression::escape(reference)));
    return pattern.match(message).hasMatch();
}

} // namespace devhub::application::planner
