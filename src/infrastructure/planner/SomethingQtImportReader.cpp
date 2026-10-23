#include "SomethingQtImportReader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <utility>

namespace devhub::infrastructure::planner {
namespace {
using Task = domain::planner::PlannerTask;
using Subtask = domain::planner::PlannerSubtask;
using CancellationToken = application::tasks::CancellationToken;

struct Column final { const char* name; const char* affinity; };
struct Table final { const char* name; const Column* columns; qsizetype count; };
constexpr Column ledgerColumns[]{{"version", "INTEGER"}, {"name", "TEXT"}};
constexpr Column taskColumns[]{{"id", "INTEGER"}, {"title", "TEXT"}, {"description", "TEXT"},
    {"is_completed", "INTEGER"}, {"created_at", "TEXT"}, {"updated_at", "TEXT"},
    {"due_date", "TEXT"}, {"priority", "TEXT"}, {"difficulty", "TEXT"}, {"revision", "INTEGER"}};
constexpr Column tagColumns[]{{"id", "INTEGER"}, {"name", "TEXT"}, {"name_normalized", "TEXT"}};
constexpr Column taskTagColumns[]{{"task_id", "INTEGER"}, {"tag_id", "INTEGER"}};
constexpr Column subtaskColumns[]{{"id", "INTEGER"}, {"task_id", "INTEGER"}, {"title", "TEXT"},
    {"is_completed", "INTEGER"}, {"position", "INTEGER"}};
constexpr Table tables[]{{"schema_migrations", ledgerColumns, std::size(ledgerColumns)},
    {"tasks", taskColumns, std::size(taskColumns)}, {"tags", tagColumns, std::size(tagColumns)},
    {"task_tags", taskTagColumns, std::size(taskTagColumns)},
    {"task_subtasks", subtaskColumns, std::size(subtaskColumns)}};

QString affinity(QString type)
{
    type = type.toUpper();
    if (type.contains(QStringLiteral("INT"))) return QStringLiteral("INTEGER");
    if (type.contains(QStringLiteral("CHAR")) || type.contains(QStringLiteral("CLOB"))
        || type.contains(QStringLiteral("TEXT"))) return QStringLiteral("TEXT");
    if (type.contains(QStringLiteral("BLOB")) || type.isEmpty()) return QStringLiteral("BLOB");
    if (type.contains(QStringLiteral("REAL")) || type.contains(QStringLiteral("FLOA"))
        || type.contains(QStringLiteral("DOUB"))) return QStringLiteral("REAL");
    return QStringLiteral("NUMERIC");
}

QString profileIdentity(const QFileInfo& info)
{
    QString canonical = QDir::cleanPath(info.canonicalFilePath());
#ifdef Q_OS_WIN
    canonical = canonical.toCaseFolded();
#endif
    const qint64 created = info.birthTime().isValid() ? info.birthTime().toUTC().toMSecsSinceEpoch() : 0;
    const QByteArray seed = canonical.toUtf8() + '\0' + QByteArray::number(created);
    return QStringLiteral("somethingqt:")
        + QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex());
}

class ReadConnection final {
public:
    ReadConnection() : m_name(QStringLiteral("devhub-somethingqt-import-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
    ~ReadConnection()
    {
        if (m_db.isValid()) {
            m_db.close();
            m_db = QSqlDatabase();
            QSqlDatabase::removeDatabase(m_name);
        }
    }
    bool open(const QString& path, QString* code, QString* identity)
    {
        const QFileInfo info(path);
        if (path.trimmed().isEmpty() || !info.exists() || !info.isFile()) {
            *code = QStringLiteral("profile_missing"); return false;
        }
        if (!info.isReadable() || info.canonicalFilePath().isEmpty()) {
            *code = QStringLiteral("profile_unreadable"); return false;
        }
        *identity = profileIdentity(info);
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        m_db.setDatabaseName(info.canonicalFilePath());
        // SQLite is allowed to read the live WAL normally. Do not use immutable
        // mode, PRAGMAs that change journal state, migrations, or checkpoints.
        m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2500"));
        if (!m_db.open()) {
            const QString error = m_db.lastError().text().toLower();
            *code = error.contains(QStringLiteral("busy")) || error.contains(QStringLiteral("locked"))
                ? QStringLiteral("database_busy") : QStringLiteral("profile_unreadable");
            return false;
        }
        if (QThread::currentThread() != m_db.driver()->thread()) {
            *code = QStringLiteral("read_failed"); return false;
        }
        return true;
    }
    QSqlDatabase& db() { return m_db; }
private:
    QString m_name;
    QSqlDatabase m_db;
};

template<typename T>
SomethingQtImportRead<T> failure(QString code, bool cancelled = false)
{
    SomethingQtImportRead<T> result;
    result.cancelled = cancelled;
    result.errorCode = std::move(code);
    if (cancelled) result.message = QStringLiteral("The SomethingQt import was cancelled.");
    else if (result.errorCode == QStringLiteral("profile_missing"))
        result.message = QStringLiteral("The selected SomethingQt database is missing. Browse to an existing SQLite profile.");
    else if (result.errorCode == QStringLiteral("profile_unreadable"))
        result.message = QStringLiteral("The selected database or its live WAL files cannot be read. Check permissions and try again.");
    else if (result.errorCode == QStringLiteral("database_busy"))
        result.message = QStringLiteral("SomethingQt is using the database exclusively. Close it or try again when SQLite permits a read.");
    else if (result.errorCode == QStringLiteral("schema_incompatible"))
        result.message = QStringLiteral("This file is not a compatible SomethingQt task database. Required tables or migration history are missing.");
    else if (result.errorCode == QStringLiteral("profile_changed"))
        result.message = QStringLiteral("The source file was replaced after verification. Verify the selected profile again.");
    else if (result.errorCode == QStringLiteral("task_missing"))
        result.message = QStringLiteral("A selected source task was deleted. Refresh the list and review the selection.");
    else if (result.errorCode == QStringLiteral("invalid_source_data"))
        result.message = QStringLiteral("A source task contains a field that DevHub cannot represent without losing data. That task was not imported.");
    else if (result.errorCode == QStringLiteral("invalid_request"))
        result.message = QStringLiteral("The import selection or page settings are invalid.");
    else
        result.message = QStringLiteral("The SomethingQt database could not be read. Check the profile and try again.");
    return result;
}

QString sqlFailureCode(const QString& detail, const QString& fallback = QStringLiteral("read_failed"))
{
    const QString lower = detail.toLower();
    return lower.contains(QStringLiteral("busy")) || lower.contains(QStringLiteral("locked"))
        ? QStringLiteral("database_busy") : fallback;
}

bool cancelled(const CancellationToken& token) { return token.isCancellationRequested(); }

bool validateSchema(QSqlDatabase& db, const CancellationToken& token, QString* error)
{
    for (const Table& table : tables) {
        if (cancelled(token)) { *error = QStringLiteral("cancelled"); return false; }
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
        exists.addBindValue(QString::fromLatin1(table.name));
        if (!exists.exec()) { *error = sqlFailureCode(exists.lastError().text(), QStringLiteral("schema_incompatible")); return false; }
        if (!exists.next()) { *error = QStringLiteral("schema_incompatible"); return false; }
        exists.finish();
        QSqlQuery columns(db);
        if (!columns.exec(QStringLiteral("PRAGMA table_info(\"%1\")").arg(QString::fromLatin1(table.name)))) {
            *error = sqlFailureCode(columns.lastError().text(), QStringLiteral("schema_incompatible")); return false;
        }
        QHash<QString, QString> found;
        while (columns.next()) found.insert(columns.value(1).toString().toCaseFolded(), affinity(columns.value(2).toString()));
        if (columns.lastError().isValid()) { *error = sqlFailureCode(columns.lastError().text(), QStringLiteral("schema_incompatible")); return false; }
        for (qsizetype i = 0; i < table.count; ++i) {
            const Column& col = table.columns[i];
            if (found.value(QString::fromLatin1(col.name).toCaseFolded()) != QString::fromLatin1(col.affinity)) {
                *error = QStringLiteral("schema_incompatible"); return false;
            }
        }
    }
    QSqlQuery history(db);
    if (!history.exec(QStringLiteral("SELECT version,name FROM schema_migrations ORDER BY version"))) {
        *error = sqlFailureCode(history.lastError().text(), QStringLiteral("schema_incompatible")); return false;
    }
    int expected = 1;
    bool foundCore = false;
    while (history.next()) {
        bool valid = false;
        const int version = history.value(0).toInt(&valid);
        const QString name = history.value(1).toString().trimmed();
        if (!valid || version != expected || name.isEmpty() || expected > 1024) {
            *error = QStringLiteral("schema_incompatible"); return false;
        }
        if (version == 1) {
            if (name != QStringLiteral("core tasks currencies preferences")) {
                *error = QStringLiteral("schema_incompatible"); return false;
            }
            foundCore = true;
        }
        ++expected;
    }
    if (history.lastError().isValid()) { *error = sqlFailureCode(history.lastError().text(), QStringLiteral("schema_incompatible")); return false; }
    if (!foundCore) { *error = QStringLiteral("schema_incompatible"); return false; }
    return !cancelled(token);
}

bool begin(ReadConnection& connection, const QString& path, const QString& expected,
           const CancellationToken& token, QString* identity, QString* error)
{
    if (cancelled(token)) { *error = QStringLiteral("cancelled"); return false; }
    if (!connection.open(path, error, identity)) return false;
    if (!expected.isEmpty() && *identity != expected) { *error = QStringLiteral("profile_changed"); return false; }
    if (!connection.db().transaction()) { *error = sqlFailureCode(connection.db().lastError().text()); return false; }
    if (!validateSchema(connection.db(), token, error)) { connection.db().rollback(); return false; }
    return true;
}

QStringList readTags(QSqlDatabase& db, qint64 taskId, QString* error)
{
    QStringList tags;
    QSet<QString> normalizedNames;
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT g.name FROM task_tags x JOIN tags g ON g.id=x.tag_id WHERE x.task_id=? ORDER BY g.name_normalized,g.id"));
    query.addBindValue(taskId);
    if (!query.exec()) { *error = sqlFailureCode(query.lastError().text()); return {}; }
    while (query.next()) {
        const QString name = query.value(0).toString();
        const QString normalized = name.trimmed().toCaseFolded();
        if (normalized.isEmpty() || normalizedNames.contains(normalized)) {
            *error = QStringLiteral("invalid_source_data");
            return {};
        }
        normalizedNames.insert(normalized);
        tags.append(name);
    }
    if (query.lastError().isValid()) *error = sqlFailureCode(query.lastError().text());
    return tags;
}

bool readTaskById(QSqlDatabase& db, qint64 id, const QString& identity, const CancellationToken& token,
                  Task* task, SomethingQtImportSummary* summary, QString* error)
{
    if (cancelled(token)) { *error = QStringLiteral("cancelled"); return false; }
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id,title,description,is_completed,created_at,updated_at,due_date,priority,difficulty,revision FROM tasks WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { *error = sqlFailureCode(query.lastError().text()); return false; }
    if (!query.next()) { *error = QStringLiteral("task_missing"); return false; }
    const qint64 sourceId = query.value(0).toLongLong();
    const QString title = query.value(1).toString();
    const QString description = query.value(2).toString();
    const bool completed = query.value(3).toBool();
    const QString created = query.value(4).toString();
    const QString updated = query.value(5).toString();
    const QString dueText = query.value(6).toString();
    const QString priority = query.value(7).toString().toCaseFolded();
    const QString difficulty = query.value(8).toString().toCaseFolded();
    const qint64 revision = query.value(9).toLongLong();
    if (sourceId != id || title.trimmed().isEmpty() || revision < 1
        || (priority != QStringLiteral("low") && priority != QStringLiteral("medium") && priority != QStringLiteral("high"))
        || (!difficulty.isEmpty() && difficulty != QStringLiteral("easy") && difficulty != QStringLiteral("medium") && difficulty != QStringLiteral("hard"))) {
        *error = QStringLiteral("invalid_source_data"); return false;
    }
    QDate dueDate;
    if (!dueText.isEmpty()) {
        dueDate = QDate::fromString(dueText, Qt::ISODate);
        if (!dueDate.isValid() || dueDate.toString(Qt::ISODate) != dueText) { *error = QStringLiteral("invalid_source_data"); return false; }
    }
    QString tagError;
    const QStringList tags = readTags(db, id, &tagError);
    if (!tagError.isEmpty()) { *error = tagError; return false; }
    QVector<Subtask> subtasks;
    QSqlQuery children(db);
    children.prepare(QStringLiteral("SELECT id,title,is_completed,position FROM task_subtasks WHERE task_id=? ORDER BY position,id"));
    children.addBindValue(id);
    if (!children.exec()) { *error = sqlFailureCode(children.lastError().text()); return false; }
    while (children.next()) {
        if (cancelled(token)) { *error = QStringLiteral("cancelled"); return false; }
        const qint64 childId = children.value(0).toLongLong();
        const qint64 position = children.value(3).toLongLong();
        if (position < 0 || position > std::numeric_limits<int>::max()) { *error = QStringLiteral("invalid_source_data"); return false; }
        subtasks.append(Subtask{{}, QString::number(childId), children.value(1).toString(),
            children.value(2).toBool(), static_cast<int>(position)});
    }
    if (children.lastError().isValid()) { *error = sqlFailureCode(children.lastError().text()); return false; }

    *task = Task{};
    task->title = title;
    task->description = description;
    task->completed = completed;
    task->status = completed ? domain::planner::PlannerStatus::Done : domain::planner::PlannerStatus::Todo;
    task->dueDate = dueDate;
    task->priority = priority;
    task->difficulty = difficulty;
    task->sourceType = QStringLiteral("somethingqt");
    task->sourceProfileIdentity = identity;
    task->sourceTaskId = QString::number(sourceId);
    task->sourceRevision = QString::number(revision);
    task->sourceCreatedAt = created;
    task->sourceUpdatedAt = updated;
    task->tags = tags;
    task->subtasks = std::move(subtasks);
    if (summary) {
        summary->sourceTaskId = task->sourceTaskId;
        summary->title = title;
        summary->completed = completed;
        summary->priority = priority;
        summary->difficulty = difficulty;
        summary->dueDate = dueText;
        summary->createdAt = created;
        summary->updatedAt = updated;
        summary->revision = task->sourceRevision;
        summary->tags = tags;
    }
    return true;
}

bool readSummaryById(QSqlDatabase& db, qint64 id, const CancellationToken& token,
                    SomethingQtImportSummary* summary, QString* error)
{
    if (cancelled(token)) { *error = QStringLiteral("cancelled"); return false; }
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id,title,is_completed,created_at,updated_at,due_date,priority,difficulty,revision FROM tasks WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) { *error = sqlFailureCode(query.lastError().text()); return false; }
    if (!query.next()) { *error = QStringLiteral("task_missing"); return false; }
    const qint64 taskId = query.value(0).toLongLong();
    const qint64 revision = query.value(8).toLongLong();
    if (taskId != id || revision < 1) { *error = QStringLiteral("invalid_source_data"); return false; }
    summary->sourceTaskId = QString::number(taskId);
    summary->title = query.value(1).toString();
    summary->completed = query.value(2).toBool();
    summary->createdAt = query.value(3).toString();
    summary->updatedAt = query.value(4).toString();
    summary->dueDate = query.value(5).toString();
    summary->priority = query.value(6).toString();
    summary->difficulty = query.value(7).toString();
    summary->revision = QString::number(revision);
    summary->tags = readTags(db, id, error);
    return error->isEmpty();
}

struct QuerySpec final { QString sql; QVariantList binds; };
void addFilters(QuerySpec* spec, const SomethingQtImportFilter& filter)
{
    if (!filter.searchText.trimmed().isEmpty()) {
        QString search = filter.searchText.trimmed();
        search.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        search.replace(QLatin1Char('%'), QStringLiteral("\\%"));
        search.replace(QLatin1Char('_'), QStringLiteral("\\_"));
        spec->sql += QStringLiteral(" AND t.title LIKE ? ESCAPE '\\' COLLATE NOCASE");
        spec->binds.append(QStringLiteral("%") + search + QStringLiteral("%"));
    }
    if (!filter.tag.trimmed().isEmpty()) {
        spec->sql += QStringLiteral(" AND EXISTS(SELECT 1 FROM task_tags ft JOIN tags fg ON fg.id=ft.tag_id WHERE ft.task_id=t.id AND fg.name_normalized=?)");
        spec->binds.append(filter.tag.trimmed().toCaseFolded());
    }
    if (!filter.priority.isEmpty()) { spec->sql += QStringLiteral(" AND t.priority=?"); spec->binds.append(filter.priority); }
    if (filter.completion >= 0) { spec->sql += QStringLiteral(" AND t.is_completed=?"); spec->binds.append(filter.completion); }
}

QuerySpec selectionQuery(const SomethingQtImportSelection& selection)
{
    QuerySpec spec{QStringLiteral("SELECT t.id FROM tasks t WHERE 1=1"), {}};
    if (!selection.tags.isEmpty()) {
        spec.sql += QStringLiteral(" AND EXISTS(SELECT 1 FROM task_tags st JOIN tags sg ON sg.id=st.tag_id WHERE st.task_id=t.id AND sg.name_normalized IN (");
        for (qsizetype i = 0; i < selection.tags.size(); ++i) {
            if (i) spec.sql += QLatin1Char(',');
            spec.sql += QLatin1Char('?');
            spec.binds.append(selection.tags.at(i).trimmed().toCaseFolded());
        }
        spec.sql += QStringLiteral("))");
    }
    spec.sql += QStringLiteral(" ORDER BY t.id");
    return spec;
}

bool run(QSqlQuery& query, const QuerySpec& spec)
{
    if (!query.prepare(spec.sql)) return false;
    for (const QVariant& bind : spec.binds) query.addBindValue(bind);
    return query.exec();
}

} // namespace

SomethingQtImportRead<SomethingQtImportProfile> SomethingQtImportReader::inspect(
    const QString& path, const CancellationToken& token)
{
    ReadConnection connection;
    QString identity, error;
    if (!begin(connection, path, {}, token, &identity, &error))
        return failure<SomethingQtImportProfile>(error, error == QStringLiteral("cancelled"));
    SomethingQtImportProfile profile;
    profile.identity = identity;
    QSqlQuery version(connection.db());
    if (!version.exec(QStringLiteral("SELECT COALESCE(MAX(version),0) FROM schema_migrations")) || !version.next())
        return failure<SomethingQtImportProfile>(sqlFailureCode(version.lastError().text(), QStringLiteral("schema_incompatible")));
    profile.schemaVersion = version.value(0).toInt();
    QSqlQuery count(connection.db());
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM tasks")) || !count.next())
        return failure<SomethingQtImportProfile>(sqlFailureCode(count.lastError().text()));
    profile.taskCount = count.value(0).toLongLong();
    QSqlQuery tags(connection.db());
    if (!tags.exec(QStringLiteral("SELECT name FROM tags ORDER BY name_normalized,name")))
        return failure<SomethingQtImportProfile>(sqlFailureCode(tags.lastError().text()));
    while (tags.next()) profile.tags.append(tags.value(0).toString());
    if (!connection.db().commit()) return failure<SomethingQtImportProfile>(sqlFailureCode(connection.db().lastError().text()));
    auto result = SomethingQtImportRead<SomethingQtImportProfile>{true, false, {}, {}, std::move(profile)};
    return result;
}

SomethingQtImportRead<SomethingQtImportPage> SomethingQtImportReader::listPage(
    const QString& path, const QString& expectedIdentity, const SomethingQtImportFilter& filter,
    const CancellationToken& token)
{
    if (filter.offset < 0 || filter.limit < 1 || filter.limit > 200 || filter.completion < -1 || filter.completion > 1)
        return failure<SomethingQtImportPage>(QStringLiteral("invalid_request"));
    ReadConnection connection;
    QString identity, error;
    if (!begin(connection, path, expectedIdentity, token, &identity, &error))
        return failure<SomethingQtImportPage>(error, error == QStringLiteral("cancelled"));
    QuerySpec spec{QStringLiteral("SELECT t.id FROM tasks t WHERE 1=1"), {}};
    addFilters(&spec, filter);
    QSqlQuery count(connection.db());
    QuerySpec countSpec{QStringLiteral("SELECT COUNT(*) FROM tasks t WHERE 1=1"), {}};
    addFilters(&countSpec, filter);
    if (!run(count, countSpec) || !count.next()) return failure<SomethingQtImportPage>(sqlFailureCode(count.lastError().text()));
    SomethingQtImportPage page;
    page.totalCount = count.value(0).toLongLong();
    spec.sql += QStringLiteral(" ORDER BY t.id DESC LIMIT ? OFFSET ?");
    spec.binds.append(filter.limit); spec.binds.append(filter.offset);
    QSqlQuery ids(connection.db());
    if (!run(ids, spec)) return failure<SomethingQtImportPage>(sqlFailureCode(ids.lastError().text()));
    while (ids.next()) {
        if (cancelled(token)) return failure<SomethingQtImportPage>(QStringLiteral("cancelled"), true);
        SomethingQtImportSummary summary;
        QString taskError;
        if (!readSummaryById(connection.db(), ids.value(0).toLongLong(), token, &summary, &taskError))
            return failure<SomethingQtImportPage>(taskError, taskError == QStringLiteral("cancelled"));
        page.tasks.append(std::move(summary));
    }
    if (ids.lastError().isValid()) return failure<SomethingQtImportPage>(sqlFailureCode(ids.lastError().text()));
    if (!connection.db().commit()) return failure<SomethingQtImportPage>(sqlFailureCode(connection.db().lastError().text()));
    return {true, false, {}, {}, std::move(page)};
}

SomethingQtImportRead<Task> SomethingQtImportReader::readTask(const QString& path,
    const QString& expectedIdentity, const QString& sourceTaskId, const CancellationToken& token)
{
    bool valid = false;
    const qint64 id = sourceTaskId.toLongLong(&valid);
    if (!valid || sourceTaskId.isEmpty()) return failure<Task>(QStringLiteral("invalid_request"));
    ReadConnection connection;
    QString identity, error;
    if (!begin(connection, path, expectedIdentity, token, &identity, &error))
        return failure<Task>(error, error == QStringLiteral("cancelled"));
    Task task;
    if (!readTaskById(connection.db(), id, identity, token, &task, nullptr, &error))
        return failure<Task>(error, error == QStringLiteral("cancelled"));
    if (!connection.db().commit()) return failure<Task>(sqlFailureCode(connection.db().lastError().text()));
    return {true, false, {}, {}, std::move(task)};
}

SomethingQtImportRead<qint64> SomethingQtImportReader::countSelection(const QString& path,
    const QString& expectedIdentity, const SomethingQtImportSelection& selection, const CancellationToken& token)
{
    ReadConnection connection;
    QString identity, error;
    if (!begin(connection, path, expectedIdentity, token, &identity, &error))
        return failure<qint64>(error, error == QStringLiteral("cancelled"));
    qint64 count = 0;
    if (!selection.sourceTaskIds.isEmpty() && !selection.all && selection.tags.isEmpty()) {
        QSet<QString> unique;
        for (const QString& sourceId : selection.sourceTaskIds) {
            bool valid = false; const qint64 id = sourceId.toLongLong(&valid);
            if (!valid) return failure<qint64>(QStringLiteral("invalid_request"));
            if (unique.contains(sourceId)) continue;
            unique.insert(sourceId);
            QSqlQuery exists(connection.db()); exists.prepare(QStringLiteral("SELECT 1 FROM tasks WHERE id=?"));
            exists.addBindValue(id);
            if (!exists.exec()) return failure<qint64>(sqlFailureCode(exists.lastError().text()));
            if (!exists.next()) return failure<qint64>(QStringLiteral("task_missing"));
            ++count;
        }
    } else {
        if (!selection.all && selection.tags.isEmpty()) return failure<qint64>(QStringLiteral("invalid_request"));
        QuerySpec spec = selectionQuery(selection);
        QSqlQuery query(connection.db());
        spec.sql.replace(QStringLiteral("SELECT t.id"), QStringLiteral("SELECT COUNT(*)"));
        if (!run(query, spec) || !query.next()) return failure<qint64>(sqlFailureCode(query.lastError().text()));
        count = query.value(0).toLongLong();
    }
    if (!connection.db().commit()) return failure<qint64>(sqlFailureCode(connection.db().lastError().text()));
    return {true, false, {}, {}, count};
}

SomethingQtImportStream SomethingQtImportReader::forEachTask(const QString& path,
    const QString& expectedIdentity, const SomethingQtImportSelection& selection,
    const CancellationToken& token, const TaskCallback& callback, const ProgressCallback& progress)
{
    ReadConnection connection;
    QString identity, error;
    if (!begin(connection, path, expectedIdentity, token, &identity, &error)) {
        auto failed = failure<int>(error, error == QStringLiteral("cancelled"));
        return {false, failed.cancelled, failed.errorCode, failed.message, 0, 0};
    }
    if (!selection.all && selection.sourceTaskIds.isEmpty() && selection.tags.isEmpty()) {
        auto failed = failure<int>(QStringLiteral("invalid_request"));
        return {false, false, failed.errorCode, failed.message, 0, 0};
    }
    SomethingQtImportStream result;
    QSet<QString> seen;
    if (!selection.sourceTaskIds.isEmpty() && !selection.all && selection.tags.isEmpty()) {
        // Validate every selected ID in the same read snapshot before the first target write.
        for (const QString& sourceId : selection.sourceTaskIds) {
            if (cancelled(token)) return {false, true, QStringLiteral("cancelled"), QStringLiteral("The SomethingQt import was cancelled."), 0, 0};
            bool valid = false; const qint64 id = sourceId.toLongLong(&valid);
            if (!valid) { auto f = failure<int>(QStringLiteral("invalid_request")); return {false, false, f.errorCode, f.message, 0, 0}; }
            if (seen.contains(sourceId)) continue;
            seen.insert(sourceId);
            QSqlQuery exists(connection.db()); exists.prepare(QStringLiteral("SELECT 1 FROM tasks WHERE id=?"));
            exists.addBindValue(id);
            if (!exists.exec()) { auto f = failure<int>(sqlFailureCode(exists.lastError().text())); return {false, false, f.errorCode, f.message, 0, 0}; }
            if (!exists.next()) { auto f = failure<int>(QStringLiteral("task_missing")); return {false, false, f.errorCode, f.message, 0, 0}; }
        }
        result.sourceCount = seen.size();
        for (const QString& sourceId : std::as_const(selection.sourceTaskIds)) {
            if (cancelled(token)) return {false, true, QStringLiteral("cancelled"), QStringLiteral("The SomethingQt import was cancelled."), result.sourceCount, result.processedCount};
            if (!seen.remove(sourceId)) continue;
            Task task;
            bool valid = false; const qint64 id = sourceId.toLongLong(&valid);
            if (!readTaskById(connection.db(), id, identity, token, &task, nullptr, &error)) {
                auto f = failure<int>(error, error == QStringLiteral("cancelled"));
                return {false, f.cancelled, f.errorCode, f.message, result.sourceCount, result.processedCount};
            }
            if (!callback(std::move(task))) return {false, token.isCancellationRequested(), token.isCancellationRequested() ? QStringLiteral("cancelled") : QStringLiteral("target_failed"), token.isCancellationRequested() ? QStringLiteral("The SomethingQt import was cancelled.") : QStringLiteral("DevHub stopped the import because it could not save a task."), result.sourceCount, result.processedCount};
            ++result.processedCount;
            if (progress) progress(result.processedCount, result.sourceCount);
        }
    } else {
        QuerySpec countSpec = selectionQuery(selection);
        countSpec.sql.replace(QStringLiteral("SELECT t.id"), QStringLiteral("SELECT COUNT(*)"));
        QSqlQuery count(connection.db());
        if (!run(count, countSpec) || !count.next()) { auto f = failure<int>(sqlFailureCode(count.lastError().text())); return {false, false, f.errorCode, f.message, 0, 0}; }
        result.sourceCount = count.value(0).toLongLong();
        QuerySpec spec = selectionQuery(selection);
        QSqlQuery ids(connection.db());
        if (!run(ids, spec)) { auto f = failure<int>(sqlFailureCode(ids.lastError().text())); return {false, false, f.errorCode, f.message, result.sourceCount, 0}; }
        while (ids.next()) {
            if (cancelled(token)) return {false, true, QStringLiteral("cancelled"), QStringLiteral("The SomethingQt import was cancelled."), result.sourceCount, result.processedCount};
            Task task;
            if (!readTaskById(connection.db(), ids.value(0).toLongLong(), identity, token, &task, nullptr, &error)) {
                auto f = failure<int>(error, error == QStringLiteral("cancelled"));
                return {false, f.cancelled, f.errorCode, f.message, result.sourceCount, result.processedCount};
            }
            if (!callback(std::move(task))) return {false, token.isCancellationRequested(), token.isCancellationRequested() ? QStringLiteral("cancelled") : QStringLiteral("target_failed"), token.isCancellationRequested() ? QStringLiteral("The SomethingQt import was cancelled.") : QStringLiteral("DevHub stopped the import because it could not save a task."), result.sourceCount, result.processedCount};
            ++result.processedCount;
            if (progress) progress(result.processedCount, result.sourceCount);
        }
        if (ids.lastError().isValid()) { auto f = failure<int>(sqlFailureCode(ids.lastError().text())); return {false, false, f.errorCode, f.message, result.sourceCount, result.processedCount}; }
    }
    if (!connection.db().commit()) { auto f = failure<int>(sqlFailureCode(connection.db().lastError().text())); return {false, false, f.errorCode, f.message, result.sourceCount, result.processedCount}; }
    result.success = true;
    return result;
}

} // namespace devhub::infrastructure::planner
