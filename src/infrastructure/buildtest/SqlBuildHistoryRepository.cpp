#include "SqlBuildHistoryRepository.h"

#include "../database/SqlErrorMapper.h"
#include "../database/SqlTransactionGuard.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

namespace devhub::infrastructure::buildtest {
namespace {
using namespace application::buildtest;
using shared::common::Result;

template<typename T>
Result<T> invalid(const QString& operation)
{
    return Result<T>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
        QStringLiteral("Build & Test data is unavailable for this workspace."), {}, operation));
}

template<typename T>
Result<T> sqlFailure(const QSqlQuery& query, const QString& operation)
{
    return Result<T>::failure(database::SqlErrorMapper::fromSqlError(query.lastError(), operation));
}

QJsonObject profileSettings(const BuildProfile& profile)
{
    QJsonObject object;
    object.insert(QStringLiteral("workingDirectory"), profile.workingDirectory);
    object.insert(QStringLiteral("preset"), profile.preset);
    object.insert(QStringLiteral("configuration"), profile.configuration);
    object.insert(QStringLiteral("buildDirectory"), profile.buildDirectory);
    object.insert(QStringLiteral("target"), profile.target);
    object.insert(QStringLiteral("testFilter"), profile.testFilter);
    object.insert(QStringLiteral("program"), profile.program);
    QJsonArray arguments;
    for (const QString& argument : profile.arguments) arguments.append(argument);
    object.insert(QStringLiteral("arguments"), arguments);
    object.insert(QStringLiteral("timeoutSeconds"), profile.timeoutSeconds);
    object.insert(QStringLiteral("testAction"), profile.testAction);
    return object;
}

BuildProfile readProfile(const QSqlQuery& query)
{
    BuildProfile profile;
    profile.id = query.value(0).toString();
    profile.workspaceId = query.value(1).toString();
    profile.name = query.value(2).toString();
    profile.runner = static_cast<RunnerType>(query.value(3).toInt());
    profile.trusted = query.value(4).toBool();
    const QJsonObject settings = QJsonDocument::fromJson(query.value(5).toByteArray()).object();
    profile.workingDirectory = settings.value(QStringLiteral("workingDirectory")).toString();
    profile.preset = settings.value(QStringLiteral("preset")).toString();
    profile.configuration = settings.value(QStringLiteral("configuration")).toString();
    profile.buildDirectory = settings.value(QStringLiteral("buildDirectory")).toString();
    profile.target = settings.value(QStringLiteral("target")).toString();
    profile.testFilter = settings.value(QStringLiteral("testFilter")).toString();
    profile.program = settings.value(QStringLiteral("program")).toString();
    for (const QJsonValue& argument : settings.value(QStringLiteral("arguments")).toArray())
        profile.arguments.append(argument.toString());
    profile.timeoutSeconds = qBound(1, settings.value(QStringLiteral("timeoutSeconds")).toInt(1800), 86400);
    profile.testAction = settings.value(QStringLiteral("testAction")).toBool();
    return profile;
}

QDateTime timestamp(const QVariant& value)
{
    QDateTime result = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!result.isValid()) result = QDateTime::fromString(value.toString(), Qt::ISODate);
    return result.toUTC();
}

} // namespace

SqlBuildHistoryRepository::SqlBuildHistoryRepository(database::InternalDatabaseManager& database,
                                                       domain::account::AccountId accountId)
    : m_database(database), m_accountId(std::move(accountId))
{
}

bool SqlBuildHistoryRepository::onOwnerThread() const
{
    return QThread::currentThread() == m_database.thread();
}

Result<QVector<BuildProfile>> SqlBuildHistoryRepository::profiles(const QString& workspaceId)
{
    if (!onOwnerThread()) return invalid<QVector<BuildProfile>>(QStringLiteral("read build profiles"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT p.id,p.workspace_id,p.name,p.runner,p.trusted,p.settings_json "
                                 "FROM build_profiles p JOIN workspaces w ON w.id=p.workspace_id "
                                 "WHERE p.workspace_id=? AND p.account_id=? AND w.account_id=? "
                                 "AND w.archived_at_utc IS NULL ORDER BY p.name,p.id"));
    query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString()); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlFailure<QVector<BuildProfile>>(query, QStringLiteral("read build profiles"));
    QVector<BuildProfile> result;
    while (query.next()) result.append(readProfile(query));
    return Result<QVector<BuildProfile>>::success(std::move(result));
}

Result<void> SqlBuildHistoryRepository::saveProfile(const BuildProfile& profile)
{
    if (!onOwnerThread()) return invalid<void>(QStringLiteral("save build profile"));
    if (profile.id.isEmpty() || profile.workspaceId.isEmpty() || profile.name.trimmed().isEmpty())
        return invalid<void>(QStringLiteral("save build profile"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO build_profiles(id,workspace_id,account_id,name,runner,trusted,settings_json,created_at_utc,updated_at_utc) "
                                 "SELECT ?,?,?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'),strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                                 "WHERE EXISTS(SELECT 1 FROM workspaces WHERE id=? AND account_id=? AND archived_at_utc IS NULL) "
                                 "ON CONFLICT(id) DO UPDATE SET name=excluded.name,runner=excluded.runner,settings_json=excluded.settings_json,"
                                 "trusted=CASE WHEN build_profiles.settings_json<>excluded.settings_json THEN 0 ELSE build_profiles.trusted END,"
                                 "updated_at_utc=excluded.updated_at_utc WHERE build_profiles.workspace_id=excluded.workspace_id "
                                 "AND build_profiles.account_id=excluded.account_id"));
    query.addBindValue(profile.id); query.addBindValue(profile.workspaceId); query.addBindValue(m_accountId.toString());
    query.addBindValue(profile.name.trimmed()); query.addBindValue(static_cast<int>(profile.runner));
    query.addBindValue(profile.trusted ? 1 : 0);
    query.addBindValue(QString::fromUtf8(QJsonDocument(profileSettings(profile)).toJson(QJsonDocument::Compact)));
    query.addBindValue(profile.workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlFailure<void>(query, QStringLiteral("save build profile"));
    if (query.numRowsAffected() != 1) return invalid<void>(QStringLiteral("save build profile"));
    return Result<void>::success();
}

Result<void> SqlBuildHistoryRepository::removeProfile(const QString& workspaceId, const QString& profileId)
{
    if (!onOwnerThread()) return invalid<void>(QStringLiteral("remove build profile"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("DELETE FROM build_profiles WHERE id=? AND workspace_id=? AND account_id=? "
                                 "AND workspace_id IN(SELECT id FROM workspaces WHERE account_id=? AND archived_at_utc IS NULL)"));
    query.addBindValue(profileId); query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString());
    query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlFailure<void>(query, QStringLiteral("remove build profile"));
    return query.numRowsAffected() == 1 ? Result<void>::success() : invalid<void>(QStringLiteral("remove build profile"));
}

Result<void> SqlBuildHistoryRepository::setProfileTrusted(const QString& workspaceId, const QString& profileId, bool trusted)
{
    if (!onOwnerThread()) return invalid<void>(QStringLiteral("update build profile trust"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("UPDATE build_profiles SET trusted=?,updated_at_utc=strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                                 "WHERE id=? AND workspace_id=? AND account_id=? AND workspace_id IN "
                                 "(SELECT id FROM workspaces WHERE account_id=? AND archived_at_utc IS NULL)"));
    query.addBindValue(trusted ? 1 : 0); query.addBindValue(profileId); query.addBindValue(workspaceId);
    query.addBindValue(m_accountId.toString()); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlFailure<void>(query, QStringLiteral("update build profile trust"));
    return query.numRowsAffected() == 1 ? Result<void>::success() : invalid<void>(QStringLiteral("update build profile trust"));
}

Result<void> SqlBuildHistoryRepository::startRun(const BuildRun& run)
{
    if (!onOwnerThread()) return invalid<void>(QStringLiteral("record build run"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("INSERT INTO build_execution_runs(id,workspace_id,account_id,profile_id,profile_name,runner,status,"
                                 "started_at_utc,finished_at_utc,duration_ms,exit_code,summary,log_reference,task_id,is_test,total_tests,passed_tests,failed_tests,skipped_tests) "
                                 "SELECT ?,?,?,?,?,?,'running',?,NULL,0,-1,'','',?,?,0,0,0,0 WHERE EXISTS("
                                 "SELECT 1 FROM workspaces WHERE id=? AND account_id=? AND archived_at_utc IS NULL)"));
    query.addBindValue(run.id); query.addBindValue(run.workspaceId); query.addBindValue(m_accountId.toString());
    query.addBindValue(run.profileId); query.addBindValue(run.profileName); query.addBindValue(static_cast<int>(run.runner));
    query.addBindValue(run.startedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(run.taskId.isEmpty() ? QVariant() : QVariant(run.taskId)); query.addBindValue(run.testAction ? 1 : 0);
    query.addBindValue(run.workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlFailure<void>(query, QStringLiteral("record build run"));
    if (query.numRowsAffected() != 1) return invalid<void>(QStringLiteral("record build run"));
    QSqlQuery prune(m_database.database());
    prune.prepare(QStringLiteral("DELETE FROM build_execution_runs WHERE id IN(SELECT id FROM build_execution_runs "
                                  "WHERE workspace_id=? AND account_id=? ORDER BY started_at_utc DESC,id DESC LIMIT -1 OFFSET 100)"));
    prune.addBindValue(run.workspaceId); prune.addBindValue(m_accountId.toString());
    if (!prune.exec()) return sqlFailure<void>(prune, QStringLiteral("prune build history"));
    return Result<void>::success();
}

Result<void> SqlBuildHistoryRepository::finishRun(const BuildRun& run)
{
    if (!onOwnerThread()) return invalid<void>(QStringLiteral("complete build run"));
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return Result<void>::failure(*transaction.error());
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("UPDATE build_execution_runs SET status=?,finished_at_utc=?,duration_ms=?,exit_code=?,summary=?,"
                                 "log_reference=?,is_test=?,total_tests=?,passed_tests=?,failed_tests=?,skipped_tests=? "
                                 "WHERE id=? AND workspace_id=? AND account_id=?"));
    query.addBindValue(static_cast<int>(run.status)); query.addBindValue(run.finishedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(run.durationMilliseconds); query.addBindValue(run.exitCode); query.addBindValue(run.summary.left(4000));
    query.addBindValue(run.logReference.isNull() ? QStringLiteral("") : run.logReference);
    query.addBindValue(run.testAction ? 1 : 0); query.addBindValue(run.totalTests); query.addBindValue(run.passedTests);
    query.addBindValue(run.failedTests); query.addBindValue(run.skippedTests); query.addBindValue(run.id);
    query.addBindValue(run.workspaceId); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlFailure<void>(query, QStringLiteral("complete build run"));
    if (query.numRowsAffected() != 1) return invalid<void>(QStringLiteral("complete build run"));
    QSqlQuery clear(m_database.database());
    clear.prepare(QStringLiteral("DELETE FROM build_test_results WHERE workspace_id=? AND run_id=?"));
    clear.addBindValue(run.workspaceId); clear.addBindValue(run.id);
    if (!clear.exec()) return sqlFailure<void>(clear, QStringLiteral("replace test results"));
    QSqlQuery insert(m_database.database());
    insert.prepare(QStringLiteral("INSERT INTO build_test_results(id,workspace_id,run_id,package_name,test_name,status,duration_ms,details) "
                                  "SELECT ?,?,?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM build_execution_runs r JOIN workspaces w ON w.id=r.workspace_id "
                                  "WHERE r.id=? AND r.workspace_id=? AND r.account_id=? AND w.account_id=? AND w.archived_at_utc IS NULL)"));
    for (const TestResult& result : run.tests) {
        insert.bindValue(0, result.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : result.id);
        insert.bindValue(1, run.workspaceId); insert.bindValue(2, run.id); insert.bindValue(3, result.packageName.left(1024));
        insert.bindValue(4, result.name.left(2048)); insert.bindValue(5, result.status.left(32));
        insert.bindValue(6, qMax<qint64>(0, result.durationMilliseconds)); insert.bindValue(7, result.details.left(8000));
        insert.bindValue(8, run.id); insert.bindValue(9, run.workspaceId); insert.bindValue(10, m_accountId.toString());
        insert.bindValue(11, m_accountId.toString());
        if (!insert.exec()) return sqlFailure<void>(insert, QStringLiteral("save test results"));
        insert.finish();
    }
    return transaction.commit();
}

Result<QVector<BuildRun>> SqlBuildHistoryRepository::recentRuns(const QString& workspaceId, int limit)
{
    if (!onOwnerThread()) return invalid<QVector<BuildRun>>(QStringLiteral("read build history"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT r.id,r.workspace_id,r.profile_id,r.profile_name,r.runner,r.status,r.started_at_utc,"
                                 "r.finished_at_utc,r.duration_ms,r.exit_code,r.summary,r.log_reference,r.task_id,"
                                 "r.total_tests,r.passed_tests,r.failed_tests,r.skipped_tests,r.is_test FROM build_execution_runs r "
                                 "JOIN workspaces w ON w.id=r.workspace_id WHERE r.workspace_id=? AND r.account_id=? "
                                 "AND w.account_id=? AND w.archived_at_utc IS NULL ORDER BY r.started_at_utc DESC,r.id DESC LIMIT ?"));
    query.addBindValue(workspaceId); query.addBindValue(m_accountId.toString()); query.addBindValue(m_accountId.toString());
    query.addBindValue(qBound(1, limit, 100));
    if (!query.exec()) return sqlFailure<QVector<BuildRun>>(query, QStringLiteral("read build history"));
    QVector<BuildRun> rows;
    while (query.next()) {
        BuildRun run;
        run.id=query.value(0).toString(); run.workspaceId=query.value(1).toString(); run.profileId=query.value(2).toString();
        run.profileName=query.value(3).toString(); run.runner=static_cast<RunnerType>(query.value(4).toInt());
        run.status=static_cast<RunStatus>(query.value(5).toInt()); run.startedAtUtc=timestamp(query.value(6));
        run.finishedAtUtc=timestamp(query.value(7)); run.durationMilliseconds=query.value(8).toLongLong();
        run.exitCode=query.value(9).toInt(); run.summary=query.value(10).toString(); run.logReference=query.value(11).toString();
        run.taskId=query.value(12).toString(); run.totalTests=query.value(13).toInt(); run.passedTests=query.value(14).toInt();
        run.failedTests=query.value(15).toInt(); run.skippedTests=query.value(16).toInt(); rows.append(std::move(run));
        rows.last().testAction=query.value(17).toBool();
    }
    return Result<QVector<BuildRun>>::success(std::move(rows));
}

Result<QVector<TestResult>> SqlBuildHistoryRepository::testResults(const QString& workspaceId, const QString& runId)
{
    if (!onOwnerThread()) return invalid<QVector<TestResult>>(QStringLiteral("read test results"));
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT t.id,t.run_id,t.package_name,t.test_name,t.status,t.duration_ms,t.details "
                                 "FROM build_test_results t JOIN build_execution_runs r ON r.workspace_id=t.workspace_id AND r.id=t.run_id "
                                 "JOIN workspaces w ON w.id=r.workspace_id WHERE t.workspace_id=? AND t.run_id=? "
                                 "AND r.account_id=? AND w.account_id=? AND w.archived_at_utc IS NULL ORDER BY t.package_name,t.test_name"));
    query.addBindValue(workspaceId); query.addBindValue(runId); query.addBindValue(m_accountId.toString()); query.addBindValue(m_accountId.toString());
    if (!query.exec()) return sqlFailure<QVector<TestResult>>(query, QStringLiteral("read test results"));
    QVector<TestResult> rows;
    while (query.next()) rows.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
        query.value(3).toString(), query.value(4).toString(), query.value(5).toLongLong(), query.value(6).toString()});
    return Result<QVector<TestResult>>::success(std::move(rows));
}

} // namespace devhub::infrastructure::buildtest
