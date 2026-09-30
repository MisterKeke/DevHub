#pragma once
#include "GitTypes.h"

namespace devhub::application::git {
class GitCommandRunner final {
public:
    explicit GitCommandRunner(QString executablePath) : m_executablePath(std::move(executablePath)) {}
    [[nodiscard]] GitCommandResult run(const GitCommand& command,
        const tasks::CancellationToken& cancellation = {}) const;
private:
    QString m_executablePath;
};
}
