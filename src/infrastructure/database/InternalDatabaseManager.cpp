#include "InternalDatabaseManager.h"

#include "SqlErrorMapper.h"
#include "SqlTransactionGuard.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <optional>
#include <utility>

namespace devhub::infrastructure::database {
namespace {

struct Migration final
{
    int version = 0;
    QString name;
    QString contents;
    bool requiresBackup = false;
};

using ErrorCode = domain::errors::ErrorCode;
using ErrorCategory = domain::errors::ErrorCategory;
using AppError = domain::errors::AppError;

shared::common::Result<void> failure(ErrorCode code,
                                     ErrorCategory category,
                                     const QString& userMessage,
                                     const QString& details,
                                     const QString& operation)
{
    return shared::common::Result<void>::failure(
        AppError::failure(code, category, userMessage, details, operation));
}

QStringList splitStatements(const QString& source)
{
    QStringList statements;
    QString current;
    bool singleQuoted = false;
    bool doubleQuoted = false;
    bool backtickQuoted = false;
    bool bracketQuoted = false;
    bool lineComment = false;
    bool blockComment = false;

    for (qsizetype index = 0; index < source.size(); ++index) {
        const QChar character = source.at(index);
        const QChar next = index + 1 < source.size() ? source.at(index + 1) : QChar();
        if (lineComment) {
            if (character == QLatin1Char('\n')) {
                lineComment = false;
                current.append(QLatin1Char('\n'));
            }
            continue;
        }
        if (blockComment) {
            if (character == QLatin1Char('*') && next == QLatin1Char('/')) {
                blockComment = false;
                ++index;
                current.append(QLatin1Char(' '));
            }
            continue;
        }
        if (!singleQuoted && !doubleQuoted && !backtickQuoted && !bracketQuoted) {
            if (character == QLatin1Char('-') && next == QLatin1Char('-')) {
                lineComment = true;
                ++index;
                continue;
            }
            if (character == QLatin1Char('/') && next == QLatin1Char('*')) {
                blockComment = true;
                ++index;
                continue;
            }
            if (character == QLatin1Char(';')) {
                if (!current.trimmed().isEmpty()) {
                    statements.append(current.trimmed());
                }
                current.clear();
                continue;
            }
        }
        if (character == QLatin1Char('\'') && !doubleQuoted && !backtickQuoted && !bracketQuoted) {
            if (singleQuoted && next == QLatin1Char('\'')) {
                current.append(character);
                current.append(next);
                ++index;
                continue;
            }
            singleQuoted = !singleQuoted;
        } else if (character == QLatin1Char('"') && !singleQuoted && !backtickQuoted && !bracketQuoted) {
            doubleQuoted = !doubleQuoted;
        } else if (character == QLatin1Char('`') && !singleQuoted && !doubleQuoted && !bracketQuoted) {
            backtickQuoted = !backtickQuoted;
        } else if (character == QLatin1Char('[') && !singleQuoted && !doubleQuoted && !backtickQuoted) {
            bracketQuoted = true;
        } else if (character == QLatin1Char(']') && bracketQuoted) {
            bracketQuoted = false;
        }
        current.append(character);
    }
    if (!current.trimmed().isEmpty()) {
        statements.append(current.trimmed());
    }
    return statements;
}

shared::common::Result<QVector<Migration>> discoverMigrations(const QString& directoryPath)
{
    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return shared::common::Result<QVector<Migration>>::failure(AppError::failure(
            ErrorCode::MigrationFailed,
            ErrorCategory::Migration,
            QStringLiteral("DevHub could not load its database updates."),
            QStringLiteral("Migration resource directory does not exist: %1").arg(directoryPath),
            QStringLiteral("discover database migrations")));
    }

    static const QRegularExpression pattern(QStringLiteral("^(\\d{4})_([a-z0-9_]+)\\.sql$"));
    QVector<Migration> migrations;
    QSet<int> versions;
    const QStringList files = directory.entryList({QStringLiteral("*.sql")}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        const QRegularExpressionMatch match = pattern.match(fileName);
        if (!match.hasMatch()) {
            return shared::common::Result<QVector<Migration>>::failure(AppError::failure(
                ErrorCode::MigrationFailed,
                ErrorCategory::Migration,
                QStringLiteral("DevHub found a database update with an invalid name."),
                QStringLiteral("Migration file must use NNNN_lower_snake_case.sql: %1").arg(fileName),
                QStringLiteral("discover database migrations")));
        }
        bool versionOk = false;
        const int version = match.captured(1).toInt(&versionOk);
        if (!versionOk || version < 1 || versions.contains(version)) {
            return shared::common::Result<QVector<Migration>>::failure(AppError::failure(
                ErrorCode::MigrationFailed,
                ErrorCategory::Migration,
                QStringLiteral("DevHub found duplicate or invalid database update versions."),
                QStringLiteral("Duplicate or invalid migration version in %1.").arg(fileName),
                QStringLiteral("discover database migrations")));
        }
        QFile file(directory.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return shared::common::Result<QVector<Migration>>::failure(AppError::failure(
                ErrorCode::MigrationFailed,
                ErrorCategory::Migration,
                QStringLiteral("DevHub could not read a database update."),
                QStringLiteral("Unable to read %1: %2").arg(fileName, file.errorString()),
                QStringLiteral("discover database migrations")));
        }
        const QString contents = QString::fromUtf8(file.readAll());
        if (contents.trimmed().isEmpty() || splitStatements(contents).isEmpty()) {
            return shared::common::Result<QVector<Migration>>::failure(AppError::failure(
                ErrorCode::MigrationFailed,
                ErrorCategory::Migration,
                QStringLiteral("DevHub found an empty database update."),
                QStringLiteral("Migration %1 has no SQL statements.").arg(fileName),
                QStringLiteral("discover database migrations")));
        }
        versions.insert(version);
        migrations.append({version, match.captured(2), contents,
                           contents.contains(QStringLiteral("-- devhub:backup-required"))});
    }
    std::sort(migrations.begin(), migrations.end(), [](const Migration& left, const Migration& right) {
        return left.version < right.version;
    });
    return shared::common::Result<QVector<Migration>>::success(std::move(migrations));
}

shared::common::Result<int> schemaVersion(QSqlDatabase database)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_migrations"))
        || !query.next()) {
        return shared::common::Result<int>::failure(SqlErrorMapper::fromSqlError(
            query.lastError(), QStringLiteral("read database schema version"), true));
    }
    return shared::common::Result<int>::success(query.value(0).toInt());
}

shared::common::Result<void> validateMigrationLedger(QSqlDatabase& database,
                                                     const QVector<Migration>& migrations,
                                                     int currentVersion)
{
    QHash<int, QString> knownMigrations;
    for (const Migration& migration : migrations) {
        knownMigrations.insert(migration.version, migration.name);
    }
    QSet<int> appliedVersions;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT version, name FROM schema_migrations ORDER BY version"))) {
        return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(
            query.lastError(), QStringLiteral("validate migration ledger"), true));
    }
    while (query.next()) {
        const int version = query.value(0).toInt();
        const QString name = query.value(1).toString();
        if (!knownMigrations.contains(version) || knownMigrations.value(version) != name) {
            return failure(ErrorCode::MigrationFailed,
                           ErrorCategory::Migration,
                           QStringLiteral("DevHub could not verify this database’s migration history."),
                           QStringLiteral("Migration ledger entry %1 (%2) does not match bundled migrations.")
                               .arg(version).arg(name),
                           QStringLiteral("validate migration ledger"));
        }
        appliedVersions.insert(version);
    }
    for (const Migration& migration : migrations) {
        if (migration.version <= currentVersion && !appliedVersions.contains(migration.version)) {
            return failure(ErrorCode::MigrationFailed,
                           ErrorCategory::Migration,
                           QStringLiteral("DevHub could not verify this database’s migration history."),
                           QStringLiteral("Migration ledger is missing applied version %1.")
                               .arg(migration.version),
                           QStringLiteral("validate migration ledger"));
        }
    }
    return shared::common::Result<void>::success();
}

} // namespace

InternalDatabaseManager::InternalDatabaseManager(QString databaseFilePath,
                                                 QString backupDirectory,
                                                 QString migrationsDirectory,
                                                 QObject* parent)
    : QObject(parent),
      m_databaseFilePath(std::move(databaseFilePath)),
      m_backupDirectory(std::move(backupDirectory)),
      m_migrationsDirectory(std::move(migrationsDirectory)),
      m_connectionName(DatabaseConnectionName::generate(QStringLiteral("internal")))
{
}

InternalDatabaseManager::~InternalDatabaseManager()
{
    const auto result = close();
    Q_ASSERT_X(result.hasValue(), "InternalDatabaseManager", "Failed to close the SQL connection in its owning thread.");
}

shared::common::Result<void> InternalDatabaseManager::ensureOwningThread(const QString& operation) const
{
    if (QThread::currentThread() == thread()) {
        return shared::common::Result<void>::success();
    }
    return failure(ErrorCode::DatabaseQueryFailed,
                   ErrorCategory::Database,
                   QStringLiteral("DevHub could not access its local database."),
                   QStringLiteral("SQL connection %1 may only be used from its owning thread.")
                       .arg(m_connectionName.value()),
                   operation);
}

shared::common::Result<void> InternalDatabaseManager::open()
{
    auto threadResult = ensureOwningThread(QStringLiteral("open internal database"));
    if (!threadResult) {
        return threadResult;
    }
    if (m_database.isOpen()) {
        return shared::common::Result<void>::success();
    }
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        return failure(ErrorCode::DatabaseUnavailable,
                       ErrorCategory::Database,
                       QStringLiteral("DevHub could not load its local database driver."),
                       QStringLiteral("The Qt QSQLITE driver is unavailable."),
                       QStringLiteral("open internal database"));
    }
    const QFileInfo databaseInfo(m_databaseFilePath);
    if (!QDir().mkpath(databaseInfo.absolutePath())) {
        return failure(ErrorCode::DatabaseUnavailable,
                       ErrorCategory::FileSystem,
                       QStringLiteral("DevHub could not prepare its local database file."),
                       QStringLiteral("Unable to create database directory: %1")
                           .arg(databaseInfo.absolutePath()),
                       QStringLiteral("prepare internal database"));
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName.value());
    m_connectionRegistered = true;
    m_database.setDatabaseName(m_databaseFilePath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!m_database.open()) {
        return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(
            m_database.lastError(), QStringLiteral("open internal database")));
    }
    return verifyDatabase();
}

shared::common::Result<void> InternalDatabaseManager::close()
{
    auto threadResult = ensureOwningThread(QStringLiteral("close internal database"));
    if (!threadResult) {
        return threadResult;
    }
    if (!m_connectionRegistered) {
        return shared::common::Result<void>::success();
    }
    const QString connectionName = m_connectionName.value();
    if (m_database.isOpen()) {
        m_database.close();
    }
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    m_connectionRegistered = false;
    return shared::common::Result<void>::success();
}

shared::common::Result<void> InternalDatabaseManager::verifyDatabase()
{
    QSqlQuery pragma(m_database);
    if (QFileInfo(m_databaseFilePath).size() > 0
        && (!pragma.exec(QStringLiteral("PRAGMA quick_check(1)")) || !pragma.next()
            || pragma.value(0).toString().compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0)) {
        const QString details = pragma.lastError().text().isEmpty()
            ? QStringLiteral("SQLite quick_check reported: %1").arg(pragma.value(0).toString())
            : pragma.lastError().text();
        return failure(ErrorCode::DatabaseCorrupt,
                       ErrorCategory::Database,
                       QStringLiteral("DevHub found a problem with its local database."),
                       details,
                       QStringLiteral("check internal database"));
    }
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))
        || !pragma.exec(QStringLiteral("PRAGMA foreign_keys")) || !pragma.next()
        || pragma.value(0).toInt() != 1) {
        return failure(ErrorCode::DatabaseUnavailable,
                       ErrorCategory::Database,
                       QStringLiteral("DevHub could not enable database integrity checks."),
                       pragma.lastError().text().isEmpty()
                           ? QStringLiteral("SQLite did not enable foreign-key enforcement.")
                           : pragma.lastError().text(),
                       QStringLiteral("configure internal database"));
    }
    if (!pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"))) {
        return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(
            pragma.lastError(), QStringLiteral("configure database busy timeout")));
    }
    if (!pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL")) || !pragma.next()) {
        return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(
            pragma.lastError(), QStringLiteral("enable database write-ahead logging")));
    }
    if (pragma.value(0).toString().compare(QStringLiteral("wal"), Qt::CaseInsensitive) != 0) {
        return failure(ErrorCode::DatabaseUnavailable,
                       ErrorCategory::Database,
                       QStringLiteral("DevHub could not enable safe database concurrency."),
                       QStringLiteral("SQLite did not switch the internal database to WAL mode."),
                       QStringLiteral("enable database write-ahead logging"));
    }

    return shared::common::Result<void>::success();
}

shared::common::Result<void> InternalDatabaseManager::createMigrationTable()
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            "version INTEGER NOT NULL PRIMARY KEY CHECK(version > 0), "
            "name TEXT NOT NULL UNIQUE, "
            "applied_at_utc TEXT NOT NULL)"))) {
        return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(
            query.lastError(), QStringLiteral("create migration ledger"), true));
    }
    return shared::common::Result<void>::success();
}

shared::common::Result<void> InternalDatabaseManager::migrate()
{
    auto threadResult = ensureOwningThread(QStringLiteral("migrate internal database"));
    if (!threadResult) {
        return threadResult;
    }
    if (!m_database.isOpen()) {
        return failure(ErrorCode::DatabaseUnavailable,
                       ErrorCategory::Database,
                       QStringLiteral("DevHub could not update its local database."),
                       QStringLiteral("Database migration requested before opening the connection."),
                       QStringLiteral("migrate internal database"));
    }

    auto migrationsResult = discoverMigrations(m_migrationsDirectory);
    if (!migrationsResult) {
        return shared::common::Result<void>::failure(migrationsResult.error());
    }
    const QVector<Migration>& migrations = migrationsResult.value();
    const int latestVersion = migrations.isEmpty() ? 0 : migrations.constLast().version;
    auto tableResult = createMigrationTable();
    if (!tableResult) {
        return tableResult;
    }
    auto versionResult = schemaVersion(m_database);
    if (!versionResult) {
        return shared::common::Result<void>::failure(versionResult.error());
    }
    int currentVersion = versionResult.value();
    if (currentVersion > latestVersion) {
        return failure(ErrorCode::SchemaTooNew,
                       ErrorCategory::Migration,
                       QStringLiteral("This database was created by a newer version of DevHub."),
                       QStringLiteral("Database schema version %1 is newer than supported version %2.")
                           .arg(currentVersion).arg(latestVersion),
                       QStringLiteral("check database schema version"));
    }
    auto ledgerResult = validateMigrationLedger(m_database, migrations, currentVersion);
    if (!ledgerResult) {
        return ledgerResult;
    }

    for (const Migration& migration : migrations) {
        if (migration.version <= currentVersion) {
            continue;
        }
        if (migration.requiresBackup) {
            auto backupResult = backupBeforeMigration(migration.version);
            if (!backupResult) {
                return backupResult;
            }
        }
        SqlTransactionGuard transaction(m_database);
        if (!transaction.active()) {
            return shared::common::Result<void>::failure(*transaction.error());
        }
        QSqlQuery query(m_database);
        const QStringList statements = splitStatements(migration.contents);
        qsizetype statementNumber = 0;
        for (const QString& statement : statements) {
            ++statementNumber;
            if (!query.exec(statement)) {
                AppError error = SqlErrorMapper::fromSqlError(
                    query.lastError(),
                    QStringLiteral("apply migration %1, statement %2")
                        .arg(migration.name).arg(statementNumber),
                    true);
                error.technicalMessage.append(QStringLiteral(" (migration version %1).")
                                                  .arg(migration.version));
                return shared::common::Result<void>::failure(std::move(error));
            }
        }
        query.prepare(QStringLiteral(
            "INSERT INTO schema_migrations(version, name, applied_at_utc) VALUES(?, ?, ?)"));
        query.addBindValue(migration.version);
        query.addBindValue(migration.name);
        query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        if (!query.exec()) {
            return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(
                query.lastError(), QStringLiteral("record applied migration %1").arg(migration.name), true));
        }
        auto commitResult = transaction.commit();
        if (!commitResult) {
            return commitResult;
        }
        currentVersion = migration.version;
        if (migration.requiresBackup) {
            pruneBackups();
        }
    }
    return shared::common::Result<void>::success();
}

shared::common::Result<void> InternalDatabaseManager::backupBeforeMigration(int version)
{
    if (!QFileInfo::exists(m_databaseFilePath) || QFileInfo(m_databaseFilePath).size() == 0) {
        return shared::common::Result<void>::success();
    }
    if (!QDir().mkpath(m_backupDirectory)) {
        return failure(ErrorCode::IoFailure,
                       ErrorCategory::FileSystem,
                       QStringLiteral("DevHub could not create a database recovery copy."),
                       QStringLiteral("Unable to create backup directory: %1").arg(m_backupDirectory),
                       QStringLiteral("back up database before migration"));
    }
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'"));
    const QString backupPath = QDir(m_backupDirectory).filePath(
        QStringLiteral("internal-pre-migration-v%1-%2-%3.db")
            .arg(version, 4, 10, QLatin1Char('0'))
            .arg(timestamp, QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString escapedPath = QDir::toNativeSeparators(QFileInfo(backupPath).absoluteFilePath());
    escapedPath.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("VACUUM INTO '%1'").arg(escapedPath))) {
        return shared::common::Result<void>::failure(SqlErrorMapper::fromSqlError(
            query.lastError(), QStringLiteral("back up database before migration"), true));
    }
    return shared::common::Result<void>::success();
}

void InternalDatabaseManager::pruneBackups() const
{
    constexpr qsizetype retainedBackupCount = 10;
    QDir directory(m_backupDirectory);
    const QFileInfoList backups = directory.entryInfoList(
        {QStringLiteral("internal-pre-migration-*.db")}, QDir::Files, QDir::Time);
    for (qsizetype index = retainedBackupCount; index < backups.size(); ++index) {
        QFile::remove(backups.at(index).absoluteFilePath());
    }
}

int InternalDatabaseManager::currentSchemaVersion() const
{
    if (!m_database.isOpen() || QThread::currentThread() != thread()) {
        return 0;
    }
    auto result = schemaVersion(m_database);
    return result ? result.value() : 0;
}

int InternalDatabaseManager::latestSchemaVersion() const
{
    auto migrations = discoverMigrations(m_migrationsDirectory);
    return migrations && !migrations.value().isEmpty() ? migrations.value().constLast().version : 0;
}

QSqlDatabase InternalDatabaseManager::database() const
{
    if (QThread::currentThread() != thread()) {
        return {};
    }
    return m_database;
}

} // namespace devhub::infrastructure::database
