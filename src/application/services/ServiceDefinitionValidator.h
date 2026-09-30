#pragma once
#include "../../domain/services/ServiceDefinition.h"
#include "../../shared/common/Result.h"

namespace devhub::application::services {
class ServiceDefinitionValidator final {
public:
    [[nodiscard]] shared::common::Result<domain::services::ServiceDefinition> validate(
        domain::services::ServiceDefinition definition, const QString& workspaceRoot) const;
};
}
