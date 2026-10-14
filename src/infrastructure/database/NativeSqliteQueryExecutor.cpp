#include "NativeSqliteQueryExecutor.h"

#include <QElapsedTimer>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QThread>
#include <QtEndian>
#include <algorithm>
#include <memory>
#include <sqlite3.h>

namespace devhub::infrastructure::database
{
using namespace application::databases;
namespace
{
struct ExecutionContext final
{
    const SqliteQueryControl &control;
    quint64 generation;
    QElapsedTimer timer;
    int timeoutMs;

    bool cancelled() const
    {
        return control.generation.load() != generation;
    }
    bool expired() const
    {
        return timer.elapsed() >= timeoutMs;
    }
};

int progress(void *value)
{
    const auto &context = *static_cast<ExecutionContext *>(value);
    return context.cancelled() || context.expired();
}

int busy(void *value, int attempts)
{
    if (progress(value) || attempts >= 250)
        return 0;
    QThread::msleep(10);
    return 1;
}

void rowFingerprint(sqlite3_context* context, int count, sqlite3_value** values)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int index = 0; index < count; ++index) {
        const int type = sqlite3_value_type(values[index]);
        const char tag = static_cast<char>(type);
        hash.addData(QByteArrayView(&tag, 1));
        QByteArray bytes;
        if (type == SQLITE_INTEGER) {
            const qint64 value = sqlite3_value_int64(values[index]);
            bytes = QByteArray::number(value);
        } else if (type == SQLITE_FLOAT) {
            bytes = QByteArray::number(sqlite3_value_double(values[index]), 'g', 17);
        } else if (type == SQLITE_TEXT) {
            const auto* data = reinterpret_cast<const char*>(sqlite3_value_text(values[index]));
            bytes = QByteArray(data, sqlite3_value_bytes(values[index]));
        } else if (type == SQLITE_BLOB) {
            const auto* data = static_cast<const char*>(sqlite3_value_blob(values[index]));
            bytes = QByteArray(data, sqlite3_value_bytes(values[index]));
        }
        const quint64 size = qToLittleEndian<quint64>(static_cast<quint64>(bytes.size()));
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(&size), static_cast<qsizetype>(sizeof(size))));
        hash.addData(bytes);
    }
    const QByteArray digest = hash.result();
    sqlite3_result_blob(context, digest.constData(), static_cast<int>(digest.size()), SQLITE_TRANSIENT);
}

QVariant readValue(sqlite3_stmt *statement, int column, qint64 &bytes)
{
    switch (sqlite3_column_type(statement, column))
    {
    case SQLITE_NULL:
        return QVariant(QMetaType(QMetaType::QString));
    case SQLITE_INTEGER:
        bytes += 16;
        return QVariant::fromValue<qlonglong>(sqlite3_column_int64(statement, column));
    case SQLITE_FLOAT:
        bytes += 16;
        return sqlite3_column_double(statement, column);
    case SQLITE_BLOB:
    {
        const int size = sqlite3_column_bytes(statement, column);
        bytes += std::min(size, 128);
        if (size > 128)
            return QStringLiteral("[BLOB, %1 bytes — preview capped]").arg(size);
        return QByteArray(static_cast<const char *>(sqlite3_column_blob(statement, column)), size);
    }
    default:
    {
        // Decode only a bounded UTF-8 prefix, even when the underlying cell is enormous.
        const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(statement, column));
        const int size = sqlite3_column_bytes(statement, column);
        QString value = QString::fromUtf8(text, std::min(size, 16384));
        if (value.size() > 4096 || size > 16384)
            value = value.left(4096) + QStringLiteral(" … [truncated]");
        bytes += value.toUtf8().size();
        return value;
    }
    }
}
} // namespace

QString configureExternalSqliteConnection(sqlite3* database)
{
    if (!database) return QStringLiteral("SQLite connection is unavailable.");
    int enabled = 0;
    int status = sqlite3_db_config(database, SQLITE_DBCONFIG_DEFENSIVE, 1, &enabled);
    if (status != SQLITE_OK || enabled != 1)
        return QStringLiteral("Could not enable SQLite defensive mode: %1").arg(QString::fromUtf8(sqlite3_errmsg(database)));
    enabled = 1;
    status = sqlite3_db_config(database, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &enabled);
    if (status != SQLITE_OK || enabled != 0)
        return QStringLiteral("Could not disable trusted SQLite schemas: %1").arg(QString::fromUtf8(sqlite3_errmsg(database)));
    return {};
}

QString registerRowFingerprintFunction(sqlite3* database)
{
    if (!database) return QStringLiteral("SQLite connection is unavailable.");
    const int status = sqlite3_create_function_v2(database, "devhub_row_fingerprint", -1,
        SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_DIRECTONLY, nullptr, rowFingerprint, nullptr, nullptr, nullptr);
    return status == SQLITE_OK ? QString{} : QString::fromUtf8(sqlite3_errmsg(database));
}

NativeSqliteQueryExecutor::NativeSqliteQueryExecutor(std::shared_ptr<SqliteQueryControl> control)
    : m_control(std::move(control))
{
}

NativeSqliteQueryExecutor::~NativeSqliteQueryExecutor()
{
    close();
}

QString NativeSqliteQueryExecutor::open(const QString &path, DatabaseOpenMode mode)
{
    close();
    const int flags =
        mode == DatabaseOpenMode::ReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    if (sqlite3_open_v2(path.toUtf8().constData(), &m_database, flags | SQLITE_OPEN_NOMUTEX,
                        nullptr) != SQLITE_OK)
    {
        const QString message = QString::fromUtf8(sqlite3_errmsg(m_database));
        close();
        return message;
    }
    sqlite3_busy_timeout(m_database, 2500);
    const QString defensiveError = configureExternalSqliteConnection(m_database);
    if (!defensiveError.isEmpty()) { close(); return defensiveError; }
    const QString fingerprintError = registerRowFingerprintFunction(m_database);
    if (!fingerprintError.isEmpty()) { close(); return fingerprintError; }
    const int configured = sqlite3_exec(m_database,
                                        mode == DatabaseOpenMode::ReadOnly
                                            ? "PRAGMA foreign_keys=ON; PRAGMA query_only=ON"
                                            : "PRAGMA foreign_keys=ON",
                                        nullptr, nullptr, nullptr);
    if (configured != SQLITE_OK)
    {
        const QString message = QString::fromUtf8(sqlite3_errmsg(m_database));
        close();
        return message;
    }
    return {};
}

void NativeSqliteQueryExecutor::close()
{
    if (m_database)
    {
        sqlite3_close_v2(m_database);
        m_database = nullptr;
    }
}

DatabaseQueryResult NativeSqliteQueryExecutor::run(const QStringList &statements,
                                                   quint64 generation,
                                                   const QVector<QVariant> &parameters,
                                                   int maximumRows, int timeoutMs)
{
    DatabaseQueryResult result;
    if (!m_database)
    {
        result.message = QStringLiteral("No database session is open.");
        return result;
    }
    ExecutionContext context{*m_control, generation, {}, std::max(1, timeoutMs)};
    context.timer.start();
    for (const QString &sql : statements)
    {
        if (progress(&context))
            break;
        sqlite3_progress_handler(m_database, 1000, progress, &context);
        sqlite3_busy_handler(m_database, busy, &context);
        sqlite3_stmt *raw = nullptr;
        const QByteArray source = sql.toUtf8();
        int status = sqlite3_prepare_v2(m_database, source.constData(),
                                        static_cast<int>(source.size()), &raw, nullptr);
        const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(
            raw, sqlite3_finalize);
        if (status != SQLITE_OK)
        {
            result.message = QString::fromUtf8(sqlite3_errmsg(m_database));
            break;
        }
        if (!raw)
            continue;
        for (qsizetype i = 0; i < parameters.size(); ++i)
        {
            const int index = static_cast<int>(i) + 1;
            const auto &value = parameters.at(i);
            if (value.isNull())
                status = sqlite3_bind_null(raw, index);
            else if (value.metaType().id() == QMetaType::Int ||
                     value.metaType().id() == QMetaType::LongLong)
                status = sqlite3_bind_int64(raw, index, value.toLongLong());
            else
            {
                const QByteArray text = value.toString().toUtf8();
                status = sqlite3_bind_text(raw, index, text.constData(),
                                           static_cast<int>(text.size()), SQLITE_TRANSIENT);
            }
            if (status != SQLITE_OK)
                break;
        }
        if (status != SQLITE_OK)
        {
            result.message = QString::fromUtf8(sqlite3_errmsg(m_database));
            break;
        }
        const int count = std::min(sqlite3_column_count(raw), 257);
        result.columns.clear();
        result.rows.clear();
        result.truncated = sqlite3_column_count(raw) > count;
        for (int i = 0; i < count; ++i)
            result.columns.append(QString::fromUtf8(sqlite3_column_name(raw, i)));
        qint64 retained = 0;
        while ((status = sqlite3_step(raw)) == SQLITE_ROW)
        {
            if (result.rows.size() >= maximumRows || retained >= 16 * 1024 * 1024)
            {
                result.truncated = true;
                break;
            }
            QVector<QVariant> row;
            for (int i = 0; i < count; ++i)
                row.append(readValue(raw, i, retained));
            result.rows.append(std::move(row));
        }
        if (status != SQLITE_DONE && status != SQLITE_ROW)
        {
            result.message = QString::fromUtf8(sqlite3_errmsg(m_database));
            break;
        }
        if (count == 0)
            result.affectedRows = sqlite3_changes64(m_database);
        if (result.truncated)
            break;
    }
    if (context.cancelled())
        result.message = QStringLiteral("SQL operation cancelled.");
    else if (context.expired())
        result.message = QStringLiteral("SQL operation exceeded its execution limit (%1 ms).")
                             .arg(context.timeoutMs);
    sqlite3_progress_handler(m_database, 0, nullptr, nullptr);
    sqlite3_busy_handler(m_database, nullptr, nullptr);
    result.durationMs = context.timer.elapsed();
    return result;
}

} // namespace devhub::infrastructure::database
