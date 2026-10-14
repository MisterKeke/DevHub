#pragma once

#include "../../application/databases/DatabaseTypes.h"

#include <atomic>
#include <memory>

struct sqlite3;

namespace devhub::infrastructure::database
{

// Shared only for cancellation; the SQLite connection stays on its owning worker thread.
struct SqliteQueryControl final
{
    std::atomic<quint64> generation{0};
};

class NativeSqliteQueryExecutor final
{
  public:
    explicit NativeSqliteQueryExecutor(std::shared_ptr<SqliteQueryControl> control);
    ~NativeSqliteQueryExecutor();
    NativeSqliteQueryExecutor(const NativeSqliteQueryExecutor &) = delete;
    NativeSqliteQueryExecutor &operator=(const NativeSqliteQueryExecutor &) = delete;

    [[nodiscard]] QString open(const QString &path, application::databases::DatabaseOpenMode mode);
    void close();
    [[nodiscard]] sqlite3* databaseHandle() const noexcept { return m_database; }
    [[nodiscard]] application::databases::DatabaseQueryResult
    run(const QStringList &statements, quint64 generation, const QVector<QVariant> &parameters = {},
        int maximumRows = 10000, int timeoutMs = 10000);

  private:
    sqlite3 *m_database = nullptr;
    std::shared_ptr<SqliteQueryControl> m_control;
};

} // namespace devhub::infrastructure::database
