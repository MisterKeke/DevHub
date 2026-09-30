#pragma once

#include "../../application/databases/ISqliteConnectionFactory.h"

namespace devhub::infrastructure::database {

class QtSqlConnectionFactory final : public application::databases::ISqliteConnectionFactory
{
public:
    [[nodiscard]] application::databases::IDatabaseSession* createSession(QObject* parent) override;
};

} // namespace devhub::infrastructure::database
