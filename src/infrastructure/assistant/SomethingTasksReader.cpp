#include "SomethingTasksReader.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>
#include <QVector>
#include <QUuid>

#include <iterator>
#include <utility>

namespace devhub::infrastructure::assistant {
namespace {

constexpr int kMaximumPageSize = 20;
constexpr int kMaximumOffset = 100000;
constexpr int kMaximumTags = 4;
constexpr int kDescriptionLimit = 8192;
constexpr int kSubtaskLimit = 50;
constexpr int kTitleLimit = 512;
constexpr int kTagDisplayLimit = 16;

struct ColumnContract final { const char* name; const char* affinity; };
struct TableContract final { const char* name; const ColumnContract* columns; qsizetype count; };
struct TaskSummaryRow final {
    qint64 id = 0;
    QString title;
    int titleLength = 0;
    bool completed = false;
    QString priority;
    QString difficulty;
    qint64 revision = 0;
    QVariant dueDate;
    QString updatedAt;
};

constexpr ColumnContract ledgerColumns[]{{"version", "INTEGER"}, {"name", "TEXT"}};
constexpr ColumnContract taskColumns[]{{"id", "INTEGER"}, {"title", "TEXT"}, {"description", "TEXT"},
    {"is_completed", "INTEGER"}, {"created_at", "TEXT"}, {"updated_at", "TEXT"}, {"due_date", "TEXT"},
    {"priority", "TEXT"}, {"difficulty", "TEXT"}, {"revision", "INTEGER"}};
constexpr ColumnContract tagColumns[]{{"id", "INTEGER"}, {"name", "TEXT"}, {"name_normalized", "TEXT"}};
constexpr ColumnContract taskTagColumns[]{{"task_id", "INTEGER"}, {"tag_id", "INTEGER"}};
constexpr ColumnContract subtaskColumns[]{{"id", "INTEGER"}, {"task_id", "INTEGER"}, {"title", "TEXT"},
    {"is_completed", "INTEGER"}, {"position", "INTEGER"}};
constexpr TableContract requiredTables[]{{"schema_migrations", ledgerColumns, std::size(ledgerColumns)},
    {"tasks", taskColumns, std::size(taskColumns)}, {"tags", tagColumns, std::size(tagColumns)},
    {"task_tags", taskTagColumns, std::size(taskTagColumns)},
    {"task_subtasks", subtaskColumns, std::size(subtaskColumns)}};

QString errorMessage(const QString& code)
{
    if (code == QStringLiteral("profile_missing"))
        return QStringLiteral("The selected SomethingQt profile file is missing. Choose an existing database file.");
    if (code == QStringLiteral("profile_unreadable"))
        return QStringLiteral("The profile database or its live WAL files are not readable. Check file permissions and try again.");
    if (code == QStringLiteral("database_busy"))
        return QStringLiteral("SomethingQt is busy with this database. Try the lookup again in a moment.");
    if (code == QStringLiteral("schema_incompatible"))
        return QStringLiteral("This file does not expose the required SomethingQt task schema. Select a compatible SomethingQt profile database.");
    if (code == QStringLiteral("task_not_found"))
        return QStringLiteral("No task with that ID exists in the selected SomethingQt profile.");
    if (code == QStringLiteral("invalid_request"))
        return QStringLiteral("The task lookup request is outside the supported limits.");
    return QStringLiteral("The SomethingQt task database could not be read. Check that the selected profile is available.");
}

SomethingTasksReadResult failure(QString code, bool cancelled = false)
{
    SomethingTasksReadResult result;
    result.cancelled = cancelled;
    result.errorCode = std::move(code);
    result.message = cancelled ? QStringLiteral("The task lookup was cancelled.") : errorMessage(result.errorCode);
    return result;
}

QString sqliteFailureCode(const QString& detail, const QString& fallback = QStringLiteral("read_failed"))
{
    const QString error = detail.toLower();
    if (error.contains(QStringLiteral("busy")) || error.contains(QStringLiteral("locked")))
        return QStringLiteral("database_busy");
    return fallback;
}

QString sqliteAffinity(QString declaredType)
{
    declaredType = declaredType.toUpper();
    if (declaredType.contains(QStringLiteral("INT"))) return QStringLiteral("INTEGER");
    if (declaredType.contains(QStringLiteral("CHAR")) || declaredType.contains(QStringLiteral("CLOB"))
        || declaredType.contains(QStringLiteral("TEXT"))) return QStringLiteral("TEXT");
    if (declaredType.contains(QStringLiteral("BLOB")) || declaredType.isEmpty()) return QStringLiteral("BLOB");
    if (declaredType.contains(QStringLiteral("REAL")) || declaredType.contains(QStringLiteral("FLOA"))
        || declaredType.contains(QStringLiteral("DOUB"))) return QStringLiteral("REAL");
    return QStringLiteral("NUMERIC");
}

class ReadOnlyConnection final {
public:
    ReadOnlyConnection() : m_name(QStringLiteral("devhub-something-tasks-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
    ~ReadOnlyConnection()
    {
        if (m_database.isValid()) {
            m_database.close();
            m_database = QSqlDatabase();
            QSqlDatabase::removeDatabase(m_name);
        }
    }

    bool open(const QString& path, QString* code)
    {
        const QFileInfo info(path);
        if (path.trimmed().isEmpty() || !info.exists() || !info.isFile()) {
            *code = QStringLiteral("profile_missing");
            return false;
        }
        if (!info.isReadable()) {
            *code = QStringLiteral("profile_unreadable");
            return false;
        }
        const QString canonicalPath = info.canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            *code = QStringLiteral("profile_unreadable");
            return false;
        }
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        m_database.setDatabaseName(canonicalPath);
        // No immutable URI, journal-mode pragma, migration, checkpoint, or copy:
        // SQLite itself handles a live WAL using normal read-only access.
        m_database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=350"));
        if (!m_database.open()) {
            const QString error = m_database.lastError().text().toLower();
            *code = error.contains(QStringLiteral("busy")) || error.contains(QStringLiteral("locked"))
                ? QStringLiteral("database_busy") : QStringLiteral("profile_unreadable");
            return false;
        }
        if (QThread::currentThread() != m_database.driver()->thread()) {
            *code = QStringLiteral("read_failed");
            return false;
        }
        return true;
    }
    QSqlDatabase& database() { return m_database; }

private:
    QString m_name;
    QSqlDatabase m_database;
};

bool checkCancellation(const application::tasks::CancellationToken& cancellation,
    SomethingTasksReadResult* result)
{
    if (!cancellation.isCancellationRequested()) return false;
    *result = failure(QStringLiteral("cancelled"), true);
    return true;
}

bool validateSchema(QSqlDatabase& db, const application::tasks::CancellationToken& cancellation,
    SomethingTasksReadResult* failureResult)
{
    if (checkCancellation(cancellation, failureResult)) return false;
    for (const auto& table : requiredTables) {
        QSqlQuery exists(db);
        if (!exists.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"))) {
            *failureResult = failure(sqliteFailureCode(exists.lastError().text(), QStringLiteral("schema_incompatible")));
            return false;
        }
        exists.addBindValue(QString::fromLatin1(table.name));
        if (!exists.exec()) {
            *failureResult = failure(sqliteFailureCode(exists.lastError().text(), QStringLiteral("schema_incompatible")));
            return false;
        }
        if (!exists.next()) {
            *failureResult = failure(sqliteFailureCode(exists.lastError().text(), QStringLiteral("schema_incompatible")));
            return false;
        }
        exists.finish();
        QSqlQuery columns(db);
        if (!columns.prepare(QStringLiteral("PRAGMA table_info(\"%1\")").arg(QString::fromLatin1(table.name)))) {
            *failureResult = failure(sqliteFailureCode(columns.lastError().text(), QStringLiteral("schema_incompatible")));
            return false;
        }
        if (!columns.exec()) { *failureResult = failure(sqliteFailureCode(columns.lastError().text(), QStringLiteral("schema_incompatible"))); return false; }
        QHash<QString, QString> found;
        while (columns.next()) found.insert(columns.value(1).toString().toCaseFolded(),
            sqliteAffinity(columns.value(2).toString()));
        if (columns.lastError().isValid()) {
            *failureResult = failure(sqliteFailureCode(columns.lastError().text(), QStringLiteral("schema_incompatible")));
            return false;
        }
        columns.finish();
        for (qsizetype i = 0; i < table.count; ++i) {
            const auto& required = table.columns[i];
            if (found.value(QString::fromLatin1(required.name).toCaseFolded())
                != QString::fromLatin1(required.affinity)) {
                *failureResult = failure(QStringLiteral("schema_incompatible"));
                return false;
            }
        }
    }

    QSqlQuery history(db);
    if (!history.exec(QStringLiteral("SELECT version,name FROM schema_migrations ORDER BY version"))) {
        *failureResult = failure(sqliteFailureCode(history.lastError().text(), QStringLiteral("schema_incompatible"))); return false;
    }
    int expected = 1;
    bool foundCore = false;
    while (history.next()) {
        if (expected > 1024) { *failureResult = failure(QStringLiteral("schema_incompatible")); return false; }
        bool validVersion = false;
        const int version = history.value(0).toInt(&validVersion);
        if (!validVersion || version != expected || history.value(1).toString().trimmed().isEmpty()) {
            *failureResult = failure(QStringLiteral("schema_incompatible")); return false;
        }
        if (version == 1) {
            if (history.value(1).toString() != QStringLiteral("core tasks currencies preferences")) {
                *failureResult = failure(QStringLiteral("schema_incompatible")); return false;
            }
            foundCore = true;
        }
        ++expected;
    }
    if (history.lastError().isValid()) {
        *failureResult = failure(sqliteFailureCode(history.lastError().text(), QStringLiteral("schema_incompatible")));
        return false;
    }
    if (!foundCore) { *failureResult = failure(QStringLiteral("schema_incompatible")); return false; }
    return !checkCancellation(cancellation, failureResult);
}

bool beginRead(QSqlDatabase& db, const application::tasks::CancellationToken& cancellation,
    SomethingTasksReadResult* result)
{
    if (checkCancellation(cancellation, result)) return false;
    if (!db.transaction()) {
        *result = failure(sqliteFailureCode(db.lastError().text()));
        return false;
    }
    if (!validateSchema(db, cancellation, result)) { db.rollback(); return false; }
    return true;
}

bool validTags(QStringList* tags)
{
    if (tags->size() < 1 || tags->size() > kMaximumTags) return false;
    QStringList normalized;
    for (QString tag : std::as_const(*tags)) {
        tag = tag.trimmed();
        if (tag.isEmpty() || tag.toUcs4().size() > 64) return false;
        const QString folded = tag.toCaseFolded();
        if (!normalized.contains(folded)) normalized.append(folded);
    }
    *tags = std::move(normalized);
    return !tags->isEmpty();
}

QJsonArray readTags(QSqlDatabase& db, qint64 taskId, const application::tasks::CancellationToken& cancellation,
    bool* truncated, SomethingTasksReadResult* result)
{
    QJsonArray output;
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("SELECT substr(g.name,1,65),length(g.name) FROM task_tags x JOIN tags g ON g.id=x.tag_id "
        "WHERE x.task_id=? ORDER BY g.name_normalized,g.id LIMIT ?"))) {
        *result = failure(sqliteFailureCode(query.lastError().text()));
        return {};
    }
    query.addBindValue(taskId);
    query.addBindValue(kTagDisplayLimit + 1);
    if (!query.exec()) { *result = failure(sqliteFailureCode(query.lastError().text())); return {}; }
    while (query.next()) {
        if (checkCancellation(cancellation, result)) return {};
        if (output.size() == kTagDisplayLimit) { *truncated = true; break; }
        if (query.value(1).toInt() > 64) *truncated = true;
        output.append(query.value(0).toString().left(64));
    }
    if (query.lastError().isValid()) {
        *result = failure(sqliteFailureCode(query.lastError().text()));
        return {};
    }
    return output;
}

QJsonObject taskSummary(QSqlDatabase& db, const TaskSummaryRow& row,
    const application::tasks::CancellationToken& cancellation, SomethingTasksReadResult* result)
{
    bool tagsTruncated = false;
    const QJsonArray tags = readTags(db, row.id, cancellation, &tagsTruncated, result);
    if (!result->errorCode.isEmpty()) return {};
    return {{QStringLiteral("taskId"), QString::number(row.id)},
        {QStringLiteral("title"), row.title}, {QStringLiteral("titleTruncated"), row.titleLength > kTitleLimit},
        {QStringLiteral("completed"), row.completed},
        {QStringLiteral("priority"), row.priority},
        {QStringLiteral("difficulty"), row.difficulty},
        {QStringLiteral("dueDate"), row.dueDate.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(row.dueDate.toString())},
        {QStringLiteral("tags"), tags}, {QStringLiteral("tagsTruncated"), tagsTruncated},
        {QStringLiteral("revision"), QString::number(row.revision)},
        {QStringLiteral("updatedAt"), row.updatedAt}};
}

SomethingTasksReadResult openedFailure(QString code)
{
    return failure(std::move(code));
}

} // namespace

SomethingTasksReadResult SomethingTasksReader::check(const QString& profilePath,
    const application::tasks::CancellationToken& cancellation)
{
    ReadOnlyConnection connection;
    QString openError;
    if (!connection.open(profilePath, &openError)) return openedFailure(openError);
    if (!connection.database().transaction()) return failure(sqliteFailureCode(connection.database().lastError().text()));
    SomethingTasksReadResult error;
    if (!validateSchema(connection.database(), cancellation, &error)) { connection.database().rollback(); return error; }
    QSqlQuery version(connection.database());
    if (!version.exec(QStringLiteral("SELECT COALESCE(MAX(version),0) FROM schema_migrations")) || !version.next()) {
        const QString code = sqliteFailureCode(version.lastError().text(), QStringLiteral("schema_incompatible"));
        connection.database().rollback(); return failure(code);
    }
    SomethingTasksReadResult result;
    result.success = true;
    result.data.insert(QStringLiteral("schemaVersion"), version.value(0).toInt());
    version.finish();
    if (!connection.database().commit()) return failure(QStringLiteral("read_failed"));
    return result;
}

SomethingTasksReadResult SomethingTasksReader::find(const QString& profilePath, QStringList tags,
    const QString& completion, int offset, int limit,
    const application::tasks::CancellationToken& cancellation)
{
    if (!validTags(&tags) || (completion != QStringLiteral("all") && completion != QStringLiteral("complete")
        && completion != QStringLiteral("incomplete")) || offset < 0 || offset > kMaximumOffset
        || limit < 1 || limit > kMaximumPageSize) return failure(QStringLiteral("invalid_request"));

    ReadOnlyConnection connection;
    QString openError;
    if (!connection.open(profilePath, &openError)) return openedFailure(openError);
    auto& db = connection.database();
    SomethingTasksReadResult result;
    if (!beginRead(db, cancellation, &result)) return result;

    QString sql = QStringLiteral("SELECT id,substr(title,1,?),length(title),is_completed,priority,difficulty,revision,due_date,updated_at FROM tasks WHERE 1=1");
    QVariantList bindings{kTitleLimit};
    if (completion == QStringLiteral("complete")) sql += QStringLiteral(" AND is_completed=1");
    else if (completion == QStringLiteral("incomplete")) sql += QStringLiteral(" AND is_completed=0");
    for (const auto& tag : std::as_const(tags)) {
        sql += QStringLiteral(" AND EXISTS(SELECT 1 FROM task_tags x JOIN tags g ON g.id=x.tag_id "
            "WHERE x.task_id=tasks.id AND g.name_normalized=?)");
        bindings.append(tag);
    }
    sql += QStringLiteral(" ORDER BY updated_at DESC,id DESC LIMIT ? OFFSET ?");
    bindings.append(limit + 1);
    bindings.append(offset);
    QSqlQuery query(db);
    query.setForwardOnly(true);
    if (!query.prepare(sql)) {
        const QString code = sqliteFailureCode(query.lastError().text());
        db.rollback(); return failure(code);
    }
    for (const auto& binding : std::as_const(bindings)) query.addBindValue(binding);
    if (!query.exec()) {
        const QString error = query.lastError().text();
        db.rollback();
        return failure(sqliteFailureCode(error));
    }
    QVector<TaskSummaryRow> selectedRows;
    bool hasMore = false;
    while (query.next()) {
        if (checkCancellation(cancellation, &result)) { query.finish(); db.rollback(); return result; }
        if (selectedRows.size() == limit) { hasMore = true; break; }
        TaskSummaryRow row;
        row.id = query.value(0).toLongLong(); row.title = query.value(1).toString();
        row.titleLength = query.value(2).toInt(); row.completed = query.value(3).toBool();
        row.priority = query.value(4).toString(); row.difficulty = query.value(5).toString();
        row.revision = query.value(6).toLongLong(); row.dueDate = query.value(7); row.updatedAt = query.value(8).toString();
        selectedRows.append(std::move(row));
    }
    query.finish();
    if (query.lastError().isValid()) { db.rollback(); return failure(sqliteFailureCode(query.lastError().text())); }
    QJsonArray rows;
    for (const auto& row : std::as_const(selectedRows)) {
        if (checkCancellation(cancellation, &result)) { db.rollback(); return result; }
        const auto summary = taskSummary(db, row, cancellation, &result);
        if (!result.errorCode.isEmpty()) { db.rollback(); return result; }
        rows.append(summary);
    }
    if (!db.commit()) return failure(QStringLiteral("read_failed"));
    result.success = true;
    result.data = {{QStringLiteral("tasks"), rows}, {QStringLiteral("hasMore"), hasMore},
        {QStringLiteral("offset"), offset}, {QStringLiteral("limit"), limit},
        {QStringLiteral("requestedTags"), QJsonArray::fromStringList(tags)},
        {QStringLiteral("completion"), completion},
        {QStringLiteral("paginationConsistency"), QStringLiteral("Fresh read per page; concurrent changes can shift offset pages.")}};
    return result;
}

SomethingTasksReadResult SomethingTasksReader::read(const QString& profilePath, const QString& taskId,
    const QString& requiredTag,
    const application::tasks::CancellationToken& cancellation)
{
    bool idOk = false;
    const qlonglong id = taskId.toLongLong(&idOk, 10);
    if (!idOk || id <= 0 || taskId != QString::number(id)) return failure(QStringLiteral("invalid_request"));
    const QString foldedRequiredTag = requiredTag.trimmed().toCaseFolded();
    if (!requiredTag.isEmpty() && (foldedRequiredTag.isEmpty() || foldedRequiredTag.toUcs4().size() > 64))
        return failure(QStringLiteral("invalid_request"));
    ReadOnlyConnection connection;
    QString openError;
    if (!connection.open(profilePath, &openError)) return openedFailure(openError);
    auto& db = connection.database();
    SomethingTasksReadResult result;
    if (!beginRead(db, cancellation, &result)) return result;

    QSqlQuery row(db);
    QString sql = QStringLiteral("SELECT id,substr(title,1,?),length(title),is_completed,priority,difficulty,revision,due_date,updated_at,substr(description,1,?),length(description),created_at FROM tasks WHERE id=?");
    if (!foldedRequiredTag.isEmpty()) sql += QStringLiteral(" AND EXISTS(SELECT 1 FROM task_tags x JOIN tags g ON g.id=x.tag_id WHERE x.task_id=tasks.id AND g.name_normalized=?)");
    if (!row.prepare(sql)) {
        const QString code = sqliteFailureCode(row.lastError().text());
        db.rollback(); return failure(code);
    }
    row.addBindValue(kTitleLimit);
    row.addBindValue(kDescriptionLimit + 1);
    row.addBindValue(id);
    if (!foldedRequiredTag.isEmpty()) row.addBindValue(foldedRequiredTag);
    if (!row.exec()) { const QString code = sqliteFailureCode(row.lastError().text()); db.rollback(); return failure(code); }
    if (!row.next()) {
        const QString code = sqliteFailureCode(row.lastError().text(), QStringLiteral("task_not_found"));
        db.rollback(); return failure(code);
    }
    if (checkCancellation(cancellation, &result)) { row.finish(); db.rollback(); return result; }
    QVariantList taskValues;
    taskValues.reserve(12);
    for (int i = 0; i < 12; ++i) taskValues.append(row.value(i));
    row.finish();

    bool tagsTruncated = false;
    const QJsonArray tags = readTags(db, id, cancellation, &tagsTruncated, &result);
    if (!result.errorCode.isEmpty()) { row.finish(); db.rollback(); return result; }
    QJsonArray subtasks;
    bool subtasksTruncated = false;
    QSqlQuery subtaskRows(db);
    if (!subtaskRows.prepare(QStringLiteral("SELECT id,substr(title,1,257),length(title),is_completed,position FROM task_subtasks WHERE task_id=? ORDER BY position,id LIMIT ?"))) {
        const QString code = sqliteFailureCode(subtaskRows.lastError().text());
        db.rollback(); return failure(code);
    }
    subtaskRows.addBindValue(id);
    subtaskRows.addBindValue(kSubtaskLimit + 1);
    if (!subtaskRows.exec()) { const QString code = sqliteFailureCode(subtaskRows.lastError().text()); row.finish(); db.rollback(); return failure(code); }
    while (subtaskRows.next()) {
        if (checkCancellation(cancellation, &result)) { subtaskRows.finish(); row.finish(); db.rollback(); return result; }
        if (subtasks.size() == kSubtaskLimit) { subtasksTruncated = true; break; }
        const QString subtaskTitle = subtaskRows.value(1).toString();
        subtasks.append(QJsonObject{{QStringLiteral("subtaskId"), QString::number(subtaskRows.value(0).toLongLong())},
            {QStringLiteral("title"), subtaskTitle.left(256)},
            {QStringLiteral("titleTruncated"), subtaskRows.value(2).toInt() > 256},
            {QStringLiteral("completed"), subtaskRows.value(3).toBool()},
            {QStringLiteral("position"), subtaskRows.value(4).toInt()}});
    }
    if (subtaskRows.lastError().isValid()) {
        const QString code = sqliteFailureCode(subtaskRows.lastError().text());
        subtaskRows.finish(); db.rollback(); return failure(code);
    }
    const QString description = taskValues.at(9).toString();
    const int descriptionLength = taskValues.at(10).toInt();
    const QString title = taskValues.at(1).toString();
    const QVariant dueDate = taskValues.at(7);
    QJsonObject task{{QStringLiteral("taskId"), QString::number(id)},
        {QStringLiteral("title"), title}, {QStringLiteral("titleTruncated"), taskValues.at(2).toInt() > kTitleLimit},
        {QStringLiteral("description"), description.left(kDescriptionLimit)},
        {QStringLiteral("descriptionTruncated"), descriptionLength > kDescriptionLimit},
        {QStringLiteral("completed"), taskValues.at(3).toBool()}, {QStringLiteral("priority"), taskValues.at(4).toString()},
        {QStringLiteral("difficulty"), taskValues.at(5).toString()}, {QStringLiteral("revision"), QString::number(taskValues.at(6).toLongLong())},
        {QStringLiteral("dueDate"), dueDate.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(dueDate.toString())},
        {QStringLiteral("createdAt"), taskValues.at(11).toString()}, {QStringLiteral("updatedAt"), taskValues.at(8).toString()},
        {QStringLiteral("tags"), tags}, {QStringLiteral("tagsTruncated"), tagsTruncated},
        {QStringLiteral("subtasks"), subtasks}, {QStringLiteral("subtasksTruncated"), subtasksTruncated}};
    subtaskRows.finish();
    if (!db.commit()) return failure(QStringLiteral("read_failed"));
    result.success = true;
    result.data.insert(QStringLiteral("task"), task);
    result.data.insert(QStringLiteral("paginationConsistency"), QStringLiteral("Task detail is one fresh read snapshot."));
    return result;
}

} // namespace devhub::infrastructure::assistant
