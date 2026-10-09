#pragma once

#include "../../application/assistant/IConversationRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../domain/account/AccountId.h"

namespace devhub::infrastructure::assistant {

class SqlConversationRepository final : public application::assistant::IConversationRepository
{
public:
    SqlConversationRepository(database::InternalDatabaseManager& database, domain::account::AccountId accountId);
    [[nodiscard]] shared::common::Result<QList<application::assistant::AssistantConversationMetadata>>
        list(const QString& accountId, int limit) override;
    [[nodiscard]] shared::common::Result<void> save(
        const application::assistant::AssistantConversationMetadata& metadata) override;

private:
    database::InternalDatabaseManager& m_database;
    domain::account::AccountId m_accountId;
};

} // namespace devhub::infrastructure::assistant
