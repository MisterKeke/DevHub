#pragma once

#include "DatabaseTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::databases {

class IDatabaseReferenceRepository
{
public:
    virtual ~IDatabaseReferenceRepository() = default;
    [[nodiscard]] virtual shared::common::Result<QVector<DatabaseReference>> list(const QString& workspaceId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> add(const DatabaseReference& reference) = 0;
    [[nodiscard]] virtual shared::common::Result<void> remove(const QString& id, const QString& workspaceId) = 0;
};

} // namespace devhub::application::databases
