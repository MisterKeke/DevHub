#include "QtSqlDatabaseWorker.h"

#include "../../application/databases/DatabaseTypes.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QUuid>

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

QStringList splitSqlStatements(const QString& source)
{
    QStringList statements;
    QString current;
    bool single = false, doubleQuote = false, backtick = false, bracket = false;
    bool lineComment = false, blockComment = false;
    for (qsizetype i = 0; i < source.size(); ++i) {
        const QChar ch = source.at(i);
        const QChar next = i + 1 < source.size() ? source.at(i + 1) : QChar();
        if (lineComment) { if (ch == QLatin1Char('\n')) { lineComment = false; current.append(ch); } continue; }
        if (blockComment) { if (ch == QLatin1Char('*') && next == QLatin1Char('/')) { blockComment = false; ++i; } continue; }
        if (!single && !doubleQuote && !backtick && !bracket) {
            if (ch == QLatin1Char('-') && next == QLatin1Char('-')) { lineComment = true; ++i; continue; }
            if (ch == QLatin1Char('/') && next == QLatin1Char('*')) { blockComment = true; ++i; continue; }
            if (ch == QLatin1Char(';')) { if (!current.trimmed().isEmpty()) statements.append(current.trimmed()); current.clear(); continue; }
        }
        if (ch == QLatin1Char('\'') && !doubleQuote && !backtick && !bracket) {
            if (single && next == QLatin1Char('\'')) { current.append(ch); current.append(next); ++i; continue; }
            single = !single;
        } else if (ch == QLatin1Char('"') && !single && !backtick && !bracket) doubleQuote = !doubleQuote;
        else if (ch == QLatin1Char('`') && !single && !doubleQuote && !bracket) backtick = !backtick;
        else if (ch == QLatin1Char('[') && !single && !doubleQuote && !backtick) bracket = true;
        else if (ch == QLatin1Char(']') && bracket) bracket = false;
        current.append(ch);
    }
    if (!current.trimmed().isEmpty()) statements.append(current.trimmed());
    return statements;
}

QString sqlError(const QSqlQuery& query) { return query.lastError().text(); }
}

QtSqlDatabaseWorker::QtSqlDatabaseWorker(QString connectionName, std::shared_ptr<SqliteQueryControl> control)
    : m_connectionName(std::move(connectionName)), m_queryExecutor(std::move(control)) {}

QString QtSqlDatabaseWorker::errorText(const QString& operation) const
{
    return QStringLiteral("%1: %2").arg(operation, m_database.lastError().text());
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
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_registered = true;
    m_database.setDatabaseName(m_filePath);
    m_database.setConnectOptions(mode == DatabaseOpenMode::ReadOnly
        ? QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2500")
        : QStringLiteral("QSQLITE_BUSY_TIMEOUT=2500"));
    if (!m_database.open()) {
        const QString message = errorText(QStringLiteral("Open SQLite database"));
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_registered = false;
        emit openFinished(false, message, mode);
        return;
    }
    QSqlQuery configure(m_database);
    if (!configure.exec(QStringLiteral("PRAGMA query_only = %1").arg(mode == DatabaseOpenMode::ReadOnly ? 1 : 0))) {
        const QString message = sqlError(configure);
        close();
        emit openFinished(false, message, mode);
        return;
    }
    configure.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    const QString nativeError = m_queryExecutor.open(m_filePath, mode);
    if (!nativeError.isEmpty()) {
        configure.finish();
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
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA table_xinfo(%1)").arg(quoted))) return columns;
    while (query.next()) {
        DatabaseColumn column;
        column.name = query.value(1).toString();
        column.declaredType = query.value(2).toString();
        column.notNull = query.value(3).toBool();
        column.defaultValue = query.value(4).toString();
        column.primaryKeyOrder = query.value(5).toInt();
        columns.append(std::move(column));
    }
    QSqlQuery foreignKeys(m_database);
    if (foreignKeys.exec(QStringLiteral("PRAGMA foreign_key_list(%1)").arg(quoted))) {
        while (foreignKeys.next()) {
            const QString from = foreignKeys.value(3).toString();
            for (auto& column : columns) if (column.name == from) {
                column.foreignTable = foreignKeys.value(2).toString();
                column.foreignColumn = foreignKeys.value(4).toString();
            }
        }
    }
    if (withoutRowId) {
        QSqlQuery sql(m_database);
        sql.prepare(QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name=?"));
        sql.addBindValue(tableName);
        *withoutRowId = sql.exec() && sql.next() && sql.value(0).toString().contains(QStringLiteral("WITHOUT ROWID"), Qt::CaseInsensitive);
    }
    return columns;
}

void QtSqlDatabaseWorker::loadSchema()
{
    QVector<DatabaseSchemaObject> objects;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT type,name,tbl_name,COALESCE(sql,'') FROM sqlite_master "
        "WHERE type IN ('table','view','index','trigger') AND name NOT LIKE 'sqlite_%' "
        "ORDER BY CASE type WHEN 'table' THEN 0 WHEN 'view' THEN 1 WHEN 'index' THEN 2 ELSE 3 END,name COLLATE NOCASE"))) {
        emit operationFailed(sqlError(query)); return;
    }
    while (query.next()) {
        const QString type = query.value(0).toString();
        const auto kind = type == QStringLiteral("table") ? DatabaseObjectKind::Table
            : type == QStringLiteral("view") ? DatabaseObjectKind::View
            : type == QStringLiteral("index") ? DatabaseObjectKind::Index : DatabaseObjectKind::Trigger;
        objects.append({kind, query.value(1).toString(), query.value(2).toString(), query.value(3).toString()});
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

    QString sql = QStringLiteral("SELECT %1 FROM %2").arg(selectExpressions.join(QLatin1Char(',')), quoteIdentifier(tableName));
    QStringList order;
    if (!sortColumn.isEmpty() && page.columns.contains(sortColumn)) order.append(quoteIdentifier(sortColumn) + (ascending ? QStringLiteral(" ASC") : QStringLiteral(" DESC")));
    else if (!page.primaryKeyColumns.isEmpty()) for (const int key : page.primaryKeyColumns) order.append(quoteIdentifier(page.columns.at(key)) + QStringLiteral(" ASC"));
    else if (!withoutRowId) order.append(QStringLiteral("rowid ASC"));
    if (!order.isEmpty()) sql += QStringLiteral(" ORDER BY ") + order.join(QLatin1Char(','));
    sql += QStringLiteral(" LIMIT ? OFFSET ?");
    auto result = m_queryExecutor.run({sql}, generation, {pageSize + 1, page.offset}, pageSize + 1);
    if (!result.message.isEmpty()) { emit operationFailed(result.message); return; }
    if (result.columns.size() != page.columns.size()) {
        emit operationFailed(QStringLiteral("This table exceeds the 256-column preview limit.")); return;
    }
    page.rows = std::move(result.rows);
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
    const QStringList statements = splitSqlStatements(sql);
    if (statements.isEmpty()) { result.message = QStringLiteral("Enter a SQL statement first."); emit queryReady(std::move(result)); return; }
    if (!allowScript && statements.size() > 1) { result.message = QStringLiteral("This contains multiple statements. Use Run Script and review its warning."); emit queryReady(std::move(result)); return; }
    result = m_queryExecutor.run(statements, generation);
    emit queryReady(std::move(result));
}

void QtSqlDatabaseWorker::backup(QString targetPath)
{
    if (!m_database.isOpen()) { emit backupFinished(false, QStringLiteral("No database session is open."), targetPath); return; }
    if (QFileInfo::exists(targetPath)) { emit backupFinished(false, QStringLiteral("The selected backup file already exists."), targetPath); return; }
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("PRAGMA query_only = OFF"));
    query.prepare(QStringLiteral("VACUUM INTO ?")); query.addBindValue(targetPath);
    if (!query.exec()) {
        const QString message = query.lastError().text();
        query.finish();
        if (m_mode == DatabaseOpenMode::ReadOnly) QSqlQuery(m_database).exec(QStringLiteral("PRAGMA query_only = ON"));
        emit backupFinished(false, message, targetPath); return;
    }
    query.finish();
    if (m_mode == DatabaseOpenMode::ReadOnly) QSqlQuery(m_database).exec(QStringLiteral("PRAGMA query_only = ON"));
    emit backupFinished(true, {}, targetPath);
}

void QtSqlDatabaseWorker::commit(PendingChanges changes)
{
    if (m_mode != DatabaseOpenMode::ReadWrite || !m_database.isOpen()) {
        emit commitFinished(false, QStringLiteral("Enable edit mode before committing changes.")); return;
    }
    bool withoutRowId = false;
    const auto columns = columnsFor(changes.tableName, &withoutRowId);
    QVector<QPair<int, QString>> keys;
    for (const auto& column : columns) if (column.primaryKeyOrder > 0) keys.append({column.primaryKeyOrder, column.name});
    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (keys.isEmpty()) { emit commitFinished(false, QStringLiteral("This table has no declared primary key. Grid edits are refused.")); return; }
    if (!m_database.transaction()) { emit commitFinished(false, m_database.lastError().text()); return; }
    const QString table = quoteIdentifier(changes.tableName);
    auto fail = [this](const QString& message) { m_database.rollback(); emit commitFinished(false, message); };

    for (const auto& edit : changes.cellEdits) {
        if (!changes.tableName.isEmpty() && edit.primaryKey.size() == keys.size()
            && std::any_of(columns.cbegin(), columns.cend(), [&edit](const DatabaseColumn& column) { return column.name == edit.columnName; })) {
            QStringList predicate; for (const auto& key : keys) predicate.append(quoteIdentifier(key.second) + QStringLiteral(" IS ?"));
            predicate.append(quoteIdentifier(edit.columnName) + QStringLiteral(" IS ?"));
            QSqlQuery query(m_database); query.prepare(QStringLiteral("UPDATE %1 SET %2=? WHERE %3")
                .arg(table, quoteIdentifier(edit.columnName), predicate.join(QStringLiteral(" AND "))));
            query.addBindValue(edit.value); for (const auto& value : edit.primaryKey) query.addBindValue(value);
            query.addBindValue(edit.originalValue);
            if (!query.exec() || query.numRowsAffected() != 1) { fail(query.lastError().isValid() ? query.lastError().text()
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
        QSqlQuery query(m_database); query.prepare(sql); for (const auto& name : names) query.addBindValue(insert.values.value(name));
        if (!query.exec() || query.numRowsAffected() != 1) { fail(query.lastError().isValid() ? query.lastError().text() : QStringLiteral("The inserted row was rejected.")); return; }
    }
    for (const auto& deletion : changes.deletes) {
        if (deletion.primaryKey.size() != keys.size()) { fail(QStringLiteral("A delete did not contain a valid stable row identity.")); return; }
        QStringList predicate; for (const auto& key : keys) predicate.append(quoteIdentifier(key.second) + QStringLiteral(" IS ?"));
        QSqlQuery query(m_database); query.prepare(QStringLiteral("DELETE FROM %1 WHERE %2").arg(table, predicate.join(QStringLiteral(" AND "))));
        for (const auto& value : deletion.primaryKey) query.addBindValue(value);
        if (!query.exec() || query.numRowsAffected() != 1) { fail(query.lastError().isValid() ? query.lastError().text()
            : QStringLiteral("The row changed or disappeared outside DevHub. No changes were committed.")); return; }
    }
    if (!m_database.commit()) { const QString message = m_database.lastError().text(); m_database.rollback(); emit commitFinished(false, message); return; }
    emit commitFinished(true, QStringLiteral("Changes committed."));
}

void QtSqlDatabaseWorker::close()
{
    m_queryExecutor.close();
    if (m_registered) {
        if (m_database.isOpen()) m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_registered = false;
    }
    emit closed();
}

} // namespace devhub::infrastructure::database

