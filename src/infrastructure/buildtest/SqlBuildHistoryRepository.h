#pragma once

#include "../../application/buildtest/IBuildHistoryRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../domain/account/AccountId.h"

namespace devhub::infrastructure::buildtest {

class SqlBuildHistoryRepository final : public application::buildtest::IBuildHistoryRepository
{
public:
    SqlBuildHistoryRepository(database::InternalDatabaseManager& database, domain::account::AccountId accountId);
    [[nodiscard]] shared::common::Result<QVector<application::buildtest::BuildProfile>> profiles(const QString& workspaceId) override;
    [[nodiscard]] shared::common::Result<void> saveProfile(const application::buildtest::BuildProfile& profile) override;
    [[nodiscard]] shared::common::Result<void> setProfileTrusted(const QString& workspaceId, const QString& profileId, bool trusted) override;
    [[nodiscard]] shared::common::Result<void> removeProfile(const QString& workspaceId, const QString& profileId) override;
    [[nodiscard]] shared::common::Result<void> startRun(const application::buildtest::BuildRun& run) override;
    [[nodiscard]] shared::common::Result<void> finishRun(const application::buildtest::BuildRun& run) override;
    [[nodiscard]] shared::common::Result<QVector<application::buildtest::BuildRun>> recentRuns(const QString& workspaceId, int limit = 50) override;
    [[nodiscard]] shared::common::Result<QVector<application::buildtest::TestResult>> testResults(const QString& workspaceId, const QString& runId) override;

private:
    [[nodiscard]] bool onOwnerThread() const;
    database::InternalDatabaseManager& m_database;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::infrastructure::buildtest
