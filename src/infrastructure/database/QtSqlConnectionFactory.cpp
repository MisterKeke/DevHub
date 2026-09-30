#include "QtSqlConnectionFactory.h"
#include "QtSqlDatabaseSession.h"

namespace devhub::infrastructure::database {
application::databases::IDatabaseSession* QtSqlConnectionFactory::createSession(QObject* parent)
{
    return new QtSqlDatabaseSession(parent);
}
}
