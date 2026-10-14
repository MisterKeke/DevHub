#include "QtSqlDatabaseWorker.h"

#include "../../application/databases/DatabaseTypes.h"

#include <QFileInfo>
#include <sqlite3.h>

#include <algorithm>

namespace devhub::infrastructure::database {
using namespace application::databases;
namespace {
QString quoteIdentifier(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QStringList splitSqlStatements(sqlite3* database, const QString& source, QString* error)
{
    QStringList statements;
    const QByteArray bytes = source.toUtf8();
    qsizetype statementStart = 0;
    auto appendStatement = [&](qsizetype end) {
        const QByteArray statementBytes = bytes.mid(statementStart, end - statementStart);
        if (statementBytes.trimmed().isEmpty()) return true;
        sqlite3_stmt* statement = nullptr;
        const int status = sqlite3_prepare_v3(database, statementBytes.constData(),
            static_cast<int>(statementBytes.size()), 0, &statement, nullptr);
        bool unresolvedTable = false;
        if (status != SQLITE_OK) {
            const QString message = QString::fromUtf8(sqlite3_errmsg(database));
            // SQLite resolves a trigger's target table while preparing it. Allow
            // unresolved schema names here so scripts can create that table first;
            // the sequential executor reports genuine missing-table errors.
            if (!message.startsWith(QStringLiteral("no such table:"), Qt::CaseInsensitive)) {
                if (error) *error = message;
                sqlite3_finalize(statement);
                return false;
            }
            unresolvedTable = true;
        }
        if (statement || unresolvedTable) {
            statements.append(QString::fromUtf8(statementBytes));
        }
        if (statement) sqlite3_finalize(statement);
        return true;
    };
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        if (bytes.at(index) != ';') continue;
        const QByteArray prefix = bytes.mid(statementStart, index - statementStart + 1);
        if (!sqlite3_complete(prefix.constData())) continue;
        if (!appendStatement(index + 1)) return {};
        statementStart = index + 1;
    }
    if (statementStart < bytes.size() && !appendStatement(bytes.size())) return {};
    return statements;
}

QString rowFingerprintSql(const QVector<DatabaseColumn>& columns)
{
    QStringList expressions;
    for (const auto& column : columns) expressions.append(quoteIdentifier(column.name));
    return QStringLiteral("devhub_row_fingerprint(%1)").arg(expressions.join(QLatin1Char(',')));
}

}

QtSqlDatabaseWorker::QtSqlDatabaseWorker(QString connectionName, std::shared_ptr<SqliteQueryControl> control)
    : m_queryControl(control), m_queryExecutor(std::move(control))
{
    setObjectName(std::move(connectionName));
}

DatabaseQueryResult QtSqlDatabaseWorker::run(const QString& sql, const QVector<QVariant>& parameters)
{
    return m_queryExecutor.run({sql}, m_queryControl->generation.load(), parameters);
}

void QtSqlDatabaseWorker::open(QString path, DatabaseOpenMode mode)
{
    close();
    m_filePath = std::move(path);
    m_mode = mode;
    const QFileInfo info(m_filePath);
    if (!info.exists() || !info.isFile()) {
        emit openFinished(false, QStringLiteral("This SQLite file does not exist. DevHub will not create a missing database."), mode);
        return;
    }
    if (mode == DatabaseOpenMode::ReadWrite && !info.isWritable()) {
        emit openFinished(false, QStringLiteral("The database file is not writable."), mode);
        return;
    }
    // The Qt QSQLITE plugin bundles a separate SQLite engine. Its handles must
    // never be passed to the native SQLite API linked into DevHub.
    const QString nativeError = m_queryExecutor.open(m_filePath, mode);
    if (!nativeError.isEmpty()) {
        close();
        emit openFinished(false, nativeError, mode);
        return;
    }
    emit openFinished(true, {}, mode);
}

QVector<DatabaseColumn> QtSqlDatabaseWorker::columnsFor(const QString& tableName, bool* withoutRowId)
{
    QVector<DatabaseColumn> columns;
    const QString quoted = quoteIdentifier(tableName);
    const auto query = run(QStringLiteral("PRAGMA table_xinfo(%1)").arg(quoted));
    if (!query.message.isEmpty()) return columns;
    for (const auto& row : query.rows) {
        DatabaseColumn column;
        column.name = row.at(1).toString();
        column.declaredType = row.at(2).toString();
        column.notNull = row.at(3).toBool();
        column.defaultValue = row.at(4).toString();
        column.primaryKeyOrder = row.at(5).toInt();
        columns.append(std::move(column));
    }
    const auto foreignKeys = run(QStringLiteral("PRAGMA foreign_key_list(%1)").arg(quoted));
    if (foreignKeys.message.isEmpty()) {
        for (const auto& row : foreignKeys.rows) {
            const QString from = row.at(3).toString();
            for (auto& column : columns) if (column.name == from) {
                column.foreignTable = row.at(2).toString();
                column.foreignColumn = row.at(4).toString();
            }
        }
    }
    if (withoutRowId) {
        const auto sql = run(QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name=?"), {tableName});
        *withoutRowId = sql.message.isEmpty() && !sql.rows.isEmpty()
            && sql.rows.first().first().toString().contains(QStringLiteral("WITHOUT ROWID"), Qt::CaseInsensitive);
    }
    return columns;
}

void QtSqlDatabaseWorker::loadSchema()
{
    QVector<DatabaseSchemaObject> objects;
    const auto query = run(QStringLiteral("SELECT type,name,tbl_name,COALESCE(sql,'') FROM sqlite_master "
        "WHERE type IN ('table','view','index','trigger') AND name NOT LIKE 'sqlite_%' "
        "ORDER BY CASE type WHEN 'table' THEN 0 WHEN 'view' THEN 1 WHEN 'index' THEN 2 ELSE 3 END,name COLLATE NOCASE"));
    if (!query.message.isEmpty()) {
        emit operationFailed(query.message); return;
    }
    for (const auto& row : query.rows) {
        const QString type = row.at(0).toString();
        const auto kind = type == QStringLiteral("table") ? DatabaseObjectKind::Table
            : type == QStringLiteral("view") ? DatabaseObjectKind::View
            : type == QStringLiteral("index") ? DatabaseObjectKind::Index : DatabaseObjectKind::Trigger;
        objects.append({kind, row.at(1).toString(), row.at(2).toString(), row.at(3).toString()});
    }
    emit schemaReady(std::move(objects));
}

void QtSqlDatabaseWorker::loadColumns(QString tableName)
{
    bool withoutRowId = false;
    const auto columns = columnsFor(tableName, &withoutRowId);
    if (columns.isEmpty()) { emit operationFailed(QStringLiteral("No column metadata was returned for %1.").arg(tableName)); return; }
    emit columnsReady(std::move(tableName), columns, withoutRowId);
}

void QtSqlDatabaseWorker::loadPage(QString tableName, int offset, int pageSize, QString sortColumn, bool ascending, quint64 generation)
{
    pageSize = qBound(25, pageSize, 500);
    bool withoutRowId = false;
    const QVector<DatabaseColumn> columns = columnsFor(tableName, &withoutRowId);
    if (columns.isEmpty()) { emit operationFailed(QStringLiteral("Could not read columns for %1.").arg(tableName)); return; }
    QStringList selectExpressions;
    DatabasePageResult page; page.pageSize = pageSize; page.offset = qMax(0, offset); page.withoutRowId = withoutRowId;
    QVector<QPair<int, int>> primaryKeys;
    for (qsizetype index = 0; index < columns.size(); ++index) {
        const auto& column = columns.at(index);
        page.columns.append(column.name);
        if (column.primaryKeyOrder > 0) primaryKeys.append({column.primaryKeyOrder, static_cast<int>(index)});
        const QString q = quoteIdentifier(column.name);
        selectExpressions.append(QStringLiteral("CASE WHEN typeof(%1)='blob' THEN '[BLOB, '||length(%1)||' bytes]' "
            "WHEN typeof(%1)='text' AND length(%1)>4096 THEN substr(%1,1,4096)||' … [truncated]' ELSE %1 END AS %2").arg(q, q));
    }
    std::sort(primaryKeys.begin(), primaryKeys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& key : primaryKeys) page.primaryKeyColumns.append(key.second);

    QStringList selected = selectExpressions;
    selected.append(rowFingerprintSql(columns));
    QString sql = QStringLiteral("SELECT %1 FROM %2").arg(selected.join(QLatin1Char(',')), quoteIdentifier(tableName));
    QStringList order;
    if (!sortColumn.isEmpty() && page.columns.contains(sortColumn)) order.append(quoteIdentifier(sortColumn) + (ascending ? QStringLiteral(" ASC") : QStringLiteral(" DESC")));
    else if (!page.primaryKeyColumns.isEmpty()) for (const int key : page.primaryKeyColumns) order.append(quoteIdentifier(page.columns.at(key)) + QStringLiteral(" ASC"));
    else if (!withoutRowId) order.append(QStringLiteral("rowid ASC"));
    if (!order.isEmpty()) sql += QStringLiteral(" ORDER BY ") + order.join(QLatin1Char(','));
    sql += QStringLiteral(" LIMIT ? OFFSET ?");
    auto result = m_queryExecutor.run({sql}, generation, {pageSize + 1, page.offset}, pageSize + 1);
    if (!result.message.isEmpty()) { emit operationFailed(result.message); return; }
    if (result.columns.size() != page.columns.size() + 1) {
        emit operationFailed(QStringLiteral("This table exceeds the 256-column preview limit.")); return;
    }
    page.rows = std::move(result.rows);
    for (auto& row : page.rows) {
        page.rowFingerprints.append(row.takeLast().toByteArray());
    }
    page.hasMore = result.truncated;
    if (page.rows.size() > pageSize) { page.rows.removeLast(); page.hasMore = true; }
    emit pageReady(std::move(tableName), std::move(page));
}

void QtSqlDatabaseWorker::countRows(QString tableName, quint64 generation)
{
    const auto result = m_queryExecutor.run({QStringLiteral("SELECT COUNT(*) FROM %1").arg(quoteIdentifier(tableName))}, generation);
    if (!result.message.isEmpty() || result.rows.isEmpty()) { emit operationFailed(result.message); return; }
    emit rowCountReady(std::move(tableName), result.rows.first().first().toLongLong());
}

void QtSqlDatabaseWorker::executeQuery(QString sql, bool allowScript, quint64 generation)
{
    DatabaseQueryResult result;
    QString parseError;
    const QStringList statements = splitSqlStatements(m_queryExecutor.databaseHandle(), sql, &parseError);
    if (!parseError.isEmpty()) { result.message = parseError; emit queryReady(std::move(result)); return; }
    if (statements.isEmpty()) { result.message = QStringLiteral("Enter a SQL statement first."); emit queryReady(std::move(result)); return; }
    if (!allowScript && statements.size() > 1) { result.message = QStringLiteral("This contains multiple statements. Use Run Script and review its warning."); emit queryReady(std::move(result)); return; }
    result = m_queryExecutor.run(statements, generation);
    emit queryReady(std::move(result));
}

void QtSqlDatabaseWorker::backup(QString targetPath)
{
    if (!m_queryExecutor.databaseHandle()) { emit backupFinished(false, QStringLiteral("No database session is open."), targetPath); return; }
    if (QFileInfo::exists(targetPath)) { emit backupFinished(false, QStringLiteral("The selected backup file already exists."), targetPath); return; }
    // VACUUM INTO writes a separate file, so temporarily allow that operation.
    // Always restore query_only, including after a failed or cancelled backup.
    const auto configured = run(QStringLiteral("PRAGMA query_only = OFF"));
    if (!configured.message.isEmpty()) { emit backupFinished(false, configured.message, targetPath); return; }
    const auto query = run(QStringLiteral("VACUUM INTO ?"), {targetPath});
    const auto restored = m_mode == DatabaseOpenMode::ReadOnly
        ? run(QStringLiteral("PRAGMA query_only = ON")) : DatabaseQueryResult{};
    if (!restored.message.isEmpty()) {
        close();
        emit backupFinished(false, restored.message, targetPath); return;
    }
    if (!query.message.isEmpty()) { emit backupFinished(false, query.message, targetPath); return; }
    emit backupFinished(true, {}, targetPath);
}

void QtSqlDatabaseWorker::commit(PendingChanges changes)
{
    if (m_mode != DatabaseOpenMode::ReadWrite || !m_queryExecutor.databaseHandle()) {
        emit commitFinished(false, QStringLiteral("Enable edit mode before committing changes.")); return;
    }
    bool withoutRowId = false;
    const auto columns = columnsFor(changes.tableName, &withoutRowId);
    QVector<QPair<int, QString>> keys;
    for (const auto& column : columns) if (column.primaryKeyOrder > 0) keys.append({column.primaryKeyOrder, column.name});
    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (keys.isEmpty()) { emit commitFinished(false, QStringLiteral("This table has no declared primary key. Grid edits are refused.")); return; }
    const auto begin = run(QStringLiteral("BEGIN IMMEDIATE"));
    if (!begin.message.isEmpty()) { emit commitFinished(false, begin.message); return; }
    const QString table = quoteIdentifier(changes.tableName);
    auto fail = [this](const QString& message) {
        // Cleanup must complete even if the operation was cancelled.
        sqlite3_exec(m_queryExecutor.databaseHandle(), "ROLLBACK", nullptr, nullptr, nullptr);
        emit commitFinished(false, message);
    };

    for (const auto& edit : changes.cellEdits) {
        if (!changes.tableName.isEmpty() && edit.primaryKey.size() == keys.size()
            && std::any_of(columns.cbegin(), columns.cend(), [&edit](const DatabaseColumn& column) { return column.name == edit.columnName; })) {
            QStringList predicate; for (const auto& key : keys) predicate.append(quoteIdentifier(key.second) + QStringLiteral(" IS ?"));
            predicate.append(quoteIdentifier(edit.columnName) + QStringLiteral(" IS ?"));
            QVector<QVariant> parameters{edit.value};
            parameters += edit.primaryKey;
            parameters.append(edit.originalValue);
            const auto query = run(QStringLiteral("UPDATE %1 SET %2=? WHERE %3")
                .arg(table, quoteIdentifier(edit.columnName), predicate.join(QStringLiteral(" AND "))), parameters);
            if (!query.message.isEmpty() || query.affectedRows != 1) { fail(!query.message.isEmpty() ? query.message
                : QStringLiteral("The row changed or disappeared outside DevHub. No changes were committed.")); return; }
        } else { fail(QStringLiteral("An edit did not contain a valid stable row identity.")); return; }
    }
    for (const auto& insert : changes.inserts) {
        QStringList names = insert.values.keys(); std::sort(names.begin(), names.end());
        QString sql;
        if (names.isEmpty()) sql = QStringLiteral("INSERT INTO %1 DEFAULT VALUES").arg(table);
        else {
            QStringList quoted; for (const auto& name : names) quoted.append(quoteIdentifier(name));
            QStringList placeholders(names.size(), QStringLiteral("?"));
            sql = QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)").arg(table, quoted.join(QLatin1Char(',')), placeholders.join(QLatin1Char(',')));
        }
        QVector<QVariant> parameters;
        for (const auto& name : names) parameters.append(insert.values.value(name));
        const auto query = run(sql, parameters);
        if (!query.message.isEmpty() || query.affectedRows != 1) { fail(!query.message.isEmpty() ? query.message : QStringLiteral("The inserted row was rejected.")); return; }
    }
    for (const auto& deletion : changes.deletes) {
        if (deletion.primaryKey.size() != keys.size() || deletion.originalFingerprint.size() != 32) {
            fail(QStringLiteral("A delete did not contain a valid stable row snapshot.")); return;
        }
        QStringList predicate; for (const auto& key : keys) predicate.append(quoteIdentifier(key.second) + QStringLiteral(" IS ?"));
        predicate.append(rowFingerprintSql(columns) + QStringLiteral(" = ?"));
        QVector<QVariant> parameters = deletion.primaryKey;
        parameters.append(deletion.originalFingerprint);
        const auto query = run(QStringLiteral("DELETE FROM %1 WHERE %2").arg(table, predicate.join(QStringLiteral(" AND "))), parameters);
        if (!query.message.isEmpty() || query.affectedRows != 1) { fail(!query.message.isEmpty() ? query.message
            : QStringLiteral("The row changed or disappeared outside DevHub. No changes were committed.")); return; }
    }
    const auto committed = run(QStringLiteral("COMMIT"));
    if (!committed.message.isEmpty()) { fail(committed.message); return; }
    emit commitFinished(true, QStringLiteral("Changes committed."));
}

void QtSqlDatabaseWorker::close()
{
    m_queryExecutor.close();
    emit closed();
}

} // namespace devhub::infrastructure::database

