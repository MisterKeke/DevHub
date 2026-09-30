#pragma once

#include "LogTypes.h"
#include "../../shared/common/Result.h"

#include <QVector>

namespace devhub::application::logs {

class ILogSourceRepository
{
public:
    virtual ~ILogSourceRepository() = default;
    [[nodiscard]] virtual shared::common::Result<QVector<LogSourceConfig>> list(const QString& workspaceId) = 0;
    [[nodiscard]] virtual shared::common::Result<void> add(const LogSourceConfig& source) = 0;
    [[nodiscard]] virtual shared::common::Result<void> remove(const QString& sourceId, const QString& workspaceId) = 0;
};

} // namespace devhub::application::logs
