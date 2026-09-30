#pragma once

#include "DatabaseConnectionName.h"
#include "../../shared/common/Result.h"

#include <QObject>
#include <QSqlDatabase>

namespace devhub::infrastructure::database {

class InternalDatabaseManager final : public QObject
{
    Q_OBJECT
public:
    explicit InternalDatabaseManager(QString databaseFilePath,
                                     QString backupDirectory,
                                     QString migrationsDirectory = QStringLiteral(":/database/migrations"),
                                     QObject* parent = nullptr);
    ~InternalDatabaseManager() override;
    InternalDatabaseManager(const InternalDatabaseManager&) = delete;
    InternalDatabaseManager& operator=(const InternalDatabaseManager&) = delete;

    [[nodiscard]] shared::common::Result<void> open();
    [[nodiscard]] shared::common::Result<void> close();
    [[nodiscard]] shared::common::Result<void> migrate();
    [[nodiscard]] int currentSchemaVersion() const;
    [[nodiscard]] int latestSchemaVersion() const;
    [[nodiscard]] QSqlDatabase database() const;
    [[nodiscard]] const QString& databaseFilePath() const noexcept { return m_databaseFilePath; }

private:
    [[nodiscard]] shared::common::Result<void> ensureOwningThread(const QString& operation) const;
    [[nodiscard]] shared::common::Result<void> createMigrationTable();
    [[nodiscard]] shared::common::Result<void> verifyDatabase();
    [[nodiscard]] shared::common::Result<void> backupBeforeMigration(int version);
    void pruneBackups() const;

    QString m_databaseFilePath;
    QString m_backupDirectory;
    QString m_migrationsDirectory;
    DatabaseConnectionName m_connectionName;
    QSqlDatabase m_database;
    bool m_connectionRegistered = false;
};

} // namespace devhub::infrastructure::database
