#pragma once
#include "AssistantTypes.h"
#include "shared/common/Result.h"
namespace devhub::application::assistant {
// Metadata only; never store transcript bodies, login challenges or tokens.
struct AssistantConversationMetadata final {
    AssistantConversation conversation;
    QString devhubAccountId;
    CodexThreadBinding binding;
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
};
class IConversationRepository {
public:
    virtual ~IConversationRepository() = default;
    virtual shared::common::Result<QList<AssistantConversationMetadata>> list(const QString& accountId, int limit) = 0;
    virtual shared::common::Result<void> save(const AssistantConversationMetadata&) = 0;
};
}
