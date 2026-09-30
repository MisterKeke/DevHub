#pragma once
#include "../../domain/services/ServiceDefinition.h"
#include "../../shared/common/Result.h"
#include <QVector>

namespace devhub::application::services {
class IServiceDefinitionRepository {
public:
    virtual ~IServiceDefinitionRepository() = default;
    [[nodiscard]] virtual shared::common::Result<QVector<domain::services::ServiceDefinition>> list(const QString& workspaceId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> save(const domain::services::ServiceDefinition& definition) = 0;
    [[nodiscard]] virtual shared::common::Result<void> remove(const QString& id, const QString& workspaceId) = 0;
};
}
