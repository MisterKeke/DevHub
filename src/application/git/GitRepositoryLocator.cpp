#include "GitRepositoryLocator.h"
#include <QDir>

namespace devhub::application::git {
shared::common::Result<GitRepositoryState> GitRepositoryLocator::locate(const QString& directory,
    const tasks::CancellationToken& cancellation) const {
    GitCommand command{QStringLiteral("repository-root"), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, directory};
    auto result = m_runner.run(command, cancellation);
    if (result.cancelled) return shared::common::Result<GitRepositoryState>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::OperationCancelled,
            domain::errors::ErrorCategory::Internal, QStringLiteral("Git discovery was cancelled."),
            QStringLiteral("Cancellation requested during rev-parse."), QStringLiteral("locate repository")));
    if (result.exitCode != 0) return shared::common::Result<GitRepositoryState>::success(GitRepositoryState{});
    GitRepositoryState state; state.isRepository = true;
    state.root = QDir::cleanPath(QString::fromLocal8Bit(result.standardOutput).trimmed());
    return shared::common::Result<GitRepositoryState>::success(std::move(state));
}
}
