#pragma once

#include "../../application/databases/DatabaseTypes.h"

#include <QObject>
#include <QSqlDatabase>

namespace devhub::infrastructure::database {

class QtSqlDatabaseWorker final : public QObject
{
    Q_OBJECT
public:
    explicit QtSqlDatabaseWorker(QString connectionName);

public slots:
    void open(QString path, devhub::application::databases::DatabaseOpenMode mode);
    void loadSchema();
    void loadColumns(QString tableName);
    void loadPage(QString tableName, int offset, int pageSize, QString sortColumn, bool ascending);
    void countRows(QString tableName);
    void executeQuery(QString sql, bool allowScript);
    void backup(QString targetPath);
    void commit(devhub::application::databases::PendingChanges changes);
    void close();

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

private:
    [[nodiscard]] QVector<devhub::application::databases::DatabaseColumn> columnsFor(const QString& tableName, bool* withoutRowId = nullptr);
    [[nodiscard]] QString errorText(const QString& operation) const;
    QString m_connectionName;
    QString m_filePath;
    QSqlDatabase m_database;
    bool m_registered = false;
    devhub::application::databases::DatabaseOpenMode m_mode = devhub::application::databases::DatabaseOpenMode::ReadOnly;
};

} // namespace devhub::infrastructure::database
