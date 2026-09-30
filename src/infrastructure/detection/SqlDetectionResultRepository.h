#pragma once

#include "../../application/detection/IDetectionResultRepository.h"
#include "../database/InternalDatabaseManager.h"
#include "../../shared/identifiers/IdGenerator.h"

namespace devhub::infrastructure::detection {

class SqlDetectionResultRepository final : public application::detection::IDetectionResultRepository
{
public:
    SqlDetectionResultRepository(database::InternalDatabaseManager& database,
                                 shared::identifiers::IIdGenerator& ids)
        : m_database(database), m_ids(ids) {}
    [[nodiscard]] shared::common::Result<void> invalidateIfVersionChanged(
        const domain::workspace::WorkspaceId& workspaceId, const QString& detectorVersion) override;
    [[nodiscard]] shared::common::Result<void> replaceCompletedScan(
        const domain::workspace::WorkspaceId& workspaceId, const QString& detectorVersion,
        const QVector<application::detection::DetectionResult>& results, qint64 inspectedFiles,
        const QString& limitReached, const QDateTime& scannedAtUtc) override;

private:
    [[nodiscard]] shared::common::Result<void> ensureReady(const QString& operation) const;
    database::InternalDatabaseManager& m_database;
    shared::identifiers::IIdGenerator& m_ids;
};

} // namespace devhub::infrastructure::detection
