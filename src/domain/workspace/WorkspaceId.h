#pragma once

#include "../../shared/common/Result.h"
#include "../../shared/identifiers/IdGenerator.h"

#include <QUuid>
#include <QString>

namespace devhub::domain::workspace {

class WorkspaceId final
{
public:
    [[nodiscard]] static shared::common::Result<WorkspaceId> create(shared::identifiers::IIdGenerator& generator);
    [[nodiscard]] static shared::common::Result<WorkspaceId> fromString(const QString& value);
    [[nodiscard]] QString toString() const { return m_value.toString(QUuid::WithoutBraces); }
    friend bool operator==(const WorkspaceId&, const WorkspaceId&) = default;

private:
    explicit WorkspaceId(QUuid value) : m_value(value) {}
    QUuid m_value;
};

} // namespace devhub::domain::workspace
