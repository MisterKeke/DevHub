#pragma once

#include "../../application/databases/IDatabaseSession.h"
#include "NativeSqliteQueryExecutor.h"

#include <QThread>

namespace devhub::infrastructure::database {

class QtSqlDatabaseWorker;

class QtSqlDatabaseSession final : public application::databases::IDatabaseSession
{
    Q_OBJECT
public:
    explicit QtSqlDatabaseSession(QObject* parent = nullptr);
    ~QtSqlDatabaseSession() override;
    void open(QString path, application::databases::DatabaseOpenMode mode) override;
    void requestSchema() override;
    void requestColumns(QString tableName) override;
    void requestPage(QString tableName, int offset, int pageSize, QString sortColumn = {}, bool ascending = true) override;
    void requestCountRows(QString tableName) override;
    void requestQuery(QString sql, bool allowScript = false) override;
    void requestBackup(QString targetPath) override;
    void requestCommit(application::databases::PendingChanges changes) override;
    void cancelQuery() override;
    void close() override;

private:
    QThread m_thread;
    std::shared_ptr<SqliteQueryControl> m_queryControl = std::make_shared<SqliteQueryControl>();
    QtSqlDatabaseWorker* m_worker = nullptr;
    bool m_threadStarted = false;
};

} // namespace devhub::infrastructure::database
