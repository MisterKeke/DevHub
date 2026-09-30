#pragma once
#include "GitCommandRunner.h"
#include "GitTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::git {
class GitRepositoryLocator final {
public:
    explicit GitRepositoryLocator(const GitCommandRunner& runner) : m_runner(runner) {}
    [[nodiscard]] shared::common::Result<GitRepositoryState> locate(const QString& directory,
        const tasks::CancellationToken& cancellation = {}) const;
private: const GitCommandRunner& m_runner;
};
}
