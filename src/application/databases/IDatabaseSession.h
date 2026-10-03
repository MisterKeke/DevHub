#pragma once

#include "DatabaseTypes.h"

#include <QObject>

namespace devhub::application::databases {

class IDatabaseSession : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void open(QString path, DatabaseOpenMode mode) = 0;
    virtual void requestSchema() = 0;
    virtual void requestColumns(QString tableName) = 0;
    virtual void requestPage(QString tableName, int offset, int pageSize, QString sortColumn = {}, bool ascending = true) = 0;
    virtual void requestCountRows(QString tableName) = 0;
    virtual void requestQuery(QString sql, bool allowScript = false) = 0;
    virtual void requestBackup(QString targetPath) = 0;
    virtual void requestCommit(PendingChanges changes) = 0;
    virtual void cancelQuery() = 0;
    virtual void close() = 0;

signals:
    void operationFailed(QString message);
    void openFinished(bool success, QString message, devhub::application::databases::DatabaseOpenMode mode);
    void schemaReady(QVector<devhub::application::databases::DatabaseSchemaObject> objects);
    void columnsReady(QString tableName, QVector<devhub::application::databases::DatabaseColumn> columns, bool withoutRowId);
    void pageReady(QString tableName, devhub::application::databases::DatabasePageResult page);
    void rowCountReady(QString tableName, qint64 count);
    void queryReady(devhub::application::databases::DatabaseQueryResult result);
    void backupFinished(bool success, QString message, QString path);
    void commitFinished(bool success, QString message);
    void closed();
};

} // namespace devhub::application::databases
