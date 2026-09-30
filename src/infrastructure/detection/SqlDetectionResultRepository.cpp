#include "SqlDetectionResultRepository.h"

#include "../database/SqlErrorMapper.h"
#include "../database/SqlTransactionGuard.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

namespace devhub::infrastructure::detection {
namespace {
QString stamp(const QDateTime& time) { return time.toUTC().toString(Qt::ISODateWithMs); }
shared::common::Result<void> invalidId()
{
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InternalFailure, domain::errors::ErrorCategory::Workspace,
        QStringLiteral("DevHub could not save workspace scan results."),
        QStringLiteral("The id generator returned a null UUID."), QStringLiteral("persist workspace scan")));
}
}

shared::common::Result<void> SqlDetectionResultRepository::ensureReady(const QString& operation) const
{
    if (QThread::currentThread() == m_database.thread() && m_database.database().isOpen())
        return shared::common::Result<void>::success();
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::DatabaseUnavailable, domain::errors::ErrorCategory::Database,
        QStringLiteral("DevHub could not access scan storage."),
        QStringLiteral("Scan result writes require the open database's owning thread."), operation));
}

shared::common::Result<void> SqlDetectionResultRepository::invalidateIfVersionChanged(
    const domain::workspace::WorkspaceId& workspaceId, const QString& detectorVersion)
{
    const QString operation = QStringLiteral("invalidate outdated workspace scan results");
    auto ready = ensureReady(operation); if (!ready) return ready;
    QSqlQuery query(m_database.database());
    query.prepare(QStringLiteral("SELECT detector_version FROM workspace_scan_state WHERE workspace_id=?"));
    query.addBindValue(workspaceId.toString());
    if (!query.exec()) return shared::common::Result<void>::failure(
        database::SqlErrorMapper::fromSqlError(query.lastError(), operation));
    if (query.next() && query.value(0).toString() == detectorVersion) return shared::common::Result<void>::success();
    query.finish();
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return shared::common::Result<void>::failure(transaction.error().value());
    QSqlQuery removeResults(m_database.database());
    removeResults.prepare(QStringLiteral("DELETE FROM workspace_detections WHERE workspace_id=?"));
    removeResults.addBindValue(workspaceId.toString());
    if (!removeResults.exec()) return shared::common::Result<void>::failure(
        database::SqlErrorMapper::fromSqlError(removeResults.lastError(), operation));
    QSqlQuery removeState(m_database.database());
    removeState.prepare(QStringLiteral("DELETE FROM workspace_scan_state WHERE workspace_id=?"));
    removeState.addBindValue(workspaceId.toString());
    if (!removeState.exec()) return shared::common::Result<void>::failure(
        database::SqlErrorMapper::fromSqlError(removeState.lastError(), operation));
    return transaction.commit();
}

shared::common::Result<void> SqlDetectionResultRepository::replaceCompletedScan(
    const domain::workspace::WorkspaceId& workspaceId, const QString& detectorVersion,
    const QVector<application::detection::DetectionResult>& results, qint64 inspectedFiles,
    const QString& limitReached, const QDateTime& scannedAtUtc)
{
    const QString operation = QStringLiteral("persist completed workspace scan");
    auto ready = ensureReady(operation); if (!ready) return ready;
    database::SqlTransactionGuard transaction(m_database.database());
    if (!transaction.active()) return shared::common::Result<void>::failure(transaction.error().value());
    QSqlQuery remove(m_database.database());
    remove.prepare(QStringLiteral("DELETE FROM workspace_detections WHERE workspace_id=?"));
    remove.addBindValue(workspaceId.toString());
    if (!remove.exec()) return shared::common::Result<void>::failure(database::SqlErrorMapper::fromSqlError(remove.lastError(), operation));

    QSqlQuery insert(m_database.database());
    insert.prepare(QStringLiteral("INSERT INTO workspace_detections(id, workspace_id, detected_path, detector_key, details_json, observed_at_utc) "
                                 "VALUES(?, ?, ?, ?, ?, ?)"));
    for (const auto& result : results) {
        for (const auto& evidence : result.evidence) {
            const QUuid rawId = m_ids.create();
            if (rawId.isNull()) return invalidId();
            QJsonObject details;
            details.insert(QStringLiteral("registryVersion"), detectorVersion);
            details.insert(QStringLiteral("detectorVersion"), evidence.detectorVersion);
            details.insert(QStringLiteral("relativeDirectory"), result.relativeDirectory);
            details.insert(QStringLiteral("description"), evidence.description);
            details.insert(QStringLiteral("confidence"), static_cast<int>(evidence.confidence));
            details.insert(QStringLiteral("inspectedFiles"), static_cast<double>(inspectedFiles));
            details.insert(QStringLiteral("limitReached"), limitReached);
            insert.bindValue(0, rawId.toString(QUuid::WithoutBraces));
            insert.bindValue(1, workspaceId.toString());
            insert.bindValue(2, evidence.relativePath);
            insert.bindValue(3, evidence.detectorId.value());
            insert.bindValue(4, QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)));
            insert.bindValue(5, stamp(scannedAtUtc));
            if (!insert.exec()) return shared::common::Result<void>::failure(
                database::SqlErrorMapper::fromSqlError(insert.lastError(), operation));
        }
    }
    QSqlQuery state(m_database.database());
    state.prepare(QStringLiteral("INSERT INTO workspace_scan_state(workspace_id, detector_version, scanned_at_utc, inspected_files, limit_reached) "
                                 "VALUES(?, ?, ?, ?, ?) ON CONFLICT(workspace_id) DO UPDATE SET "
                                 "detector_version=excluded.detector_version, scanned_at_utc=excluded.scanned_at_utc, "
                                 "inspected_files=excluded.inspected_files, limit_reached=excluded.limit_reached"));
    state.addBindValue(workspaceId.toString()); state.addBindValue(detectorVersion);
    state.addBindValue(stamp(scannedAtUtc)); state.addBindValue(inspectedFiles); state.addBindValue(limitReached);
    if (!state.exec()) return shared::common::Result<void>::failure(
        database::SqlErrorMapper::fromSqlError(state.lastError(), operation));
    return transaction.commit();
}

} // namespace devhub::infrastructure::detection
