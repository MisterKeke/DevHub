#pragma once
#include "GitCommandRunner.h"
#include "GitTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::git {
class GitService final {
public:
    explicit GitService(GitCommandRunner runner) : m_runner(std::move(runner)) {}
    [[nodiscard]] shared::common::Result<GitRepositoryState> status(const QString& directory,
        const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<QVector<GitBranchState>> branches(const QString& directory,
        bool remote, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<QVector<GitRemote>> remotes(const QString& directory,
        const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<QVector<GitCommit>> history(const QString& directory, int offset = 0,
        int limit = 50, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitDiff> diff(const QString& directory, bool staged,
        const QString& commit = {}, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> mutate(const QString& directory,
        QString operation, QStringList arguments, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> stage(const QString& directory,
        const QStringList& paths, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> unstage(const QString& directory,
        const QStringList& paths, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> commit(const QString& directory,
        const QString& summary, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> fetch(const QString& directory, const QString& remote,
        const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> pull(const QString& directory, const QString& remote,
        const QString& branch, const QString& explicitStrategy, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> push(const QString& directory, const QString& remote,
        const QString& branch, bool setUpstream, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> checkout(const QString& directory, const QString& branch,
        const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> createBranch(const QString& directory, const QString& branch,
        const QString& startPoint, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> renameBranch(const QString& directory, const QString& oldName,
        const QString& newName, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> deleteBranch(const QString& directory, const QString& name,
        const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> stashCreate(const QString& directory, const QString& message,
        const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<QStringList> stashList(const QString& directory,
        const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> stashApply(const QString& directory, const QString& stash,
        bool pop, const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> merge(const QString& directory, const QString& branch,
        const tasks::CancellationToken& cancellation = {}) const;
    [[nodiscard]] shared::common::Result<GitCommandResult> abortMerge(const QString& directory,
        const tasks::CancellationToken& cancellation = {}) const;
private: GitCommandRunner m_runner;
};
}
