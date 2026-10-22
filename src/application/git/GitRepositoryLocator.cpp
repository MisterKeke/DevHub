#include "GitRepositoryLocator.h"
#include <QDir>
#include <QFileInfo>

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
    const QString root = QString::fromLocal8Bit(result.standardOutput).trimmed();
    state.root = QFileInfo(root).canonicalFilePath();
    if (state.root.isEmpty()) state.root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const auto readPath = [this, &cancellation, &state](const QStringList& arguments) {
        const GitCommand query{QStringLiteral("repository-identity"), arguments, state.root};
        const auto value = m_runner.run(query, cancellation);
        if (value.exitCode != 0 || value.timedOut || value.cancelled) return QString{};
        const QString path = QString::fromLocal8Bit(value.standardOutput).trimmed();
        if (path.isEmpty()) return QString{};
        const QString canonical = QFileInfo(path).canonicalFilePath();
        return canonical.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : canonical;
    };
    state.gitDirectory = readPath({QStringLiteral("rev-parse"), QStringLiteral("--path-format=absolute"),
        QStringLiteral("--absolute-git-dir")});
    state.commonDirectory = readPath({QStringLiteral("rev-parse"), QStringLiteral("--path-format=absolute"),
        QStringLiteral("--git-common-dir")});
    if (state.commonDirectory.isEmpty()) state.commonDirectory = state.gitDirectory;
    return shared::common::Result<GitRepositoryState>::success(std::move(state));
}
}
