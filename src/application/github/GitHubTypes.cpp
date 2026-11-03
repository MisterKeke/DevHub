#include "GitHubTypes.h"

namespace devhub::application::github {

QString pullRequestStatusText(const GitHubPullRequest& pullRequest)
{
    if (pullRequest.merged) return QObject::tr("Merged");
    if (pullRequest.state.compare(QStringLiteral("closed"), Qt::CaseInsensitive) == 0)
        return QObject::tr("Closed");
    return pullRequest.draft ? QObject::tr("Draft") : QObject::tr("Open");
}

QString checkRunStatusText(const QString& status, const QString& conclusion)
{
    if (status == QStringLiteral("queued")) return QObject::tr("Queued");
    if (status == QStringLiteral("pending")) return QObject::tr("Pending");
    if (status == QStringLiteral("in_progress") || status == QStringLiteral("requested")
        || status == QStringLiteral("waiting") || status == QStringLiteral("pending"))
        return QObject::tr("In progress");
    if (status != QStringLiteral("completed")) return QObject::tr("Unknown");
    if (conclusion == QStringLiteral("success")) return QObject::tr("Success");
    if (conclusion == QStringLiteral("failure") || conclusion == QStringLiteral("timed_out")
        || conclusion == QStringLiteral("action_required")) return QObject::tr("Failure");
    if (conclusion == QStringLiteral("cancelled")) return QObject::tr("Cancelled");
    if (conclusion == QStringLiteral("skipped")) return QObject::tr("Skipped");
    if (conclusion == QStringLiteral("neutral")) return QObject::tr("Neutral");
    return QObject::tr("Unknown");
}

} // namespace devhub::application::github
