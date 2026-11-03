#pragma once

#include "BuildTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::buildtest {

class IBuildHistoryRepository
{
public:
    virtual ~IBuildHistoryRepository() = default;
    [[nodiscard]] virtual shared::common::Result<QVector<BuildProfile>> profiles(const QString& workspaceId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> saveProfile(const BuildProfile& profile) = 0;
    [[nodiscard]] virtual shared::common::Result<void> setProfileTrusted(const QString& workspaceId, const QString& profileId, bool trusted) = 0;
    [[nodiscard]] virtual shared::common::Result<void> removeProfile(const QString& workspaceId, const QString& profileId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> startRun(const BuildRun& run) = 0;
    [[nodiscard]] virtual shared::common::Result<void> finishRun(const BuildRun& run) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<BuildRun>> recentRuns(const QString& workspaceId, int limit = 50) = 0;
    [[nodiscard]] virtual shared::common::Result<QVector<TestResult>> testResults(const QString& workspaceId, const QString& runId) = 0;
};

} // namespace devhub::application::buildtest
