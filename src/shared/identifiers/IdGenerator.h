#pragma once

#include <QUuid>

namespace devhub::shared::identifiers {

class IIdGenerator
{
public:
    virtual ~IIdGenerator() = default;
    [[nodiscard]] virtual QUuid create() = 0;
};

class UuidIdGenerator final : public IIdGenerator
{
public:
    [[nodiscard]] QUuid create() override;
};

} // namespace devhub::shared::identifiers
