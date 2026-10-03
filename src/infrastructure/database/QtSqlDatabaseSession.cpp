#include "QtSqlDatabaseSession.h"

#include "QtSqlDatabaseWorker.h"

#include <QMetaObject>
#include <QUuid>

namespace devhub::infrastructure::database {
using namespace application::databases;

QtSqlDatabaseSession::QtSqlDatabaseSession(QObject* parent)
    : IDatabaseSession(parent), m_worker(new QtSqlDatabaseWorker(
          QStringLiteral("devhub-workspace-db-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_queryControl))
{
    qRegisterMetaType<DatabaseOpenMode>();
    qRegisterMetaType<QVector<DatabaseSchemaObject>>();
    qRegisterMetaType<QVector<DatabaseColumn>>();
    qRegisterMetaType<DatabasePageResult>();
    qRegisterMetaType<DatabaseQueryResult>();
    qRegisterMetaType<PendingChanges>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &QtSqlDatabaseWorker::openFinished, this, &IDatabaseSession::openFinished);
    connect(m_worker, &QtSqlDatabaseWorker::operationFailed, this, &IDatabaseSession::operationFailed);
    connect(m_worker, &QtSqlDatabaseWorker::schemaReady, this, &IDatabaseSession::schemaReady);
    connect(m_worker, &QtSqlDatabaseWorker::columnsReady, this, &IDatabaseSession::columnsReady);
    connect(m_worker, &QtSqlDatabaseWorker::pageReady, this, &IDatabaseSession::pageReady);
    connect(m_worker, &QtSqlDatabaseWorker::rowCountReady, this, &IDatabaseSession::rowCountReady);
    connect(m_worker, &QtSqlDatabaseWorker::queryReady, this, &IDatabaseSession::queryReady);
    connect(m_worker, &QtSqlDatabaseWorker::backupFinished, this, &IDatabaseSession::backupFinished);
    connect(m_worker, &QtSqlDatabaseWorker::commitFinished, this, &IDatabaseSession::commitFinished);
    connect(m_worker, &QtSqlDatabaseWorker::closed, this, &IDatabaseSession::closed);
    m_thread.setObjectName(QStringLiteral("DevHub SQLite session"));
    m_thread.start();
    m_threadStarted = true;
}

QtSqlDatabaseSession::~QtSqlDatabaseSession()
{
    if (m_threadStarted && m_thread.isRunning()) {
        cancelQuery();
        QMetaObject::invokeMethod(m_worker, "close", Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
        m_worker = nullptr;
    }
}

void QtSqlDatabaseSession::open(QString path, DatabaseOpenMode mode)
{ QMetaObject::invokeMethod(m_worker, "open", Qt::QueuedConnection, Q_ARG(QString, std::move(path)), Q_ARG(DatabaseOpenMode, mode)); }
void QtSqlDatabaseSession::requestSchema() { QMetaObject::invokeMethod(m_worker, "loadSchema", Qt::QueuedConnection); }
void QtSqlDatabaseSession::requestColumns(QString name)
{ QMetaObject::invokeMethod(m_worker, "loadColumns", Qt::QueuedConnection, Q_ARG(QString, std::move(name))); }
void QtSqlDatabaseSession::requestPage(QString table, int offset, int size, QString sort, bool ascending)
{
    size = qBound(25, size, 500);
    QMetaObject::invokeMethod(m_worker, "loadPage", Qt::QueuedConnection,
        Q_ARG(QString, std::move(table)), Q_ARG(int, qMax(0, offset)), Q_ARG(int, size), Q_ARG(QString, std::move(sort)), Q_ARG(bool, ascending), Q_ARG(quint64, m_queryControl->generation.load()));
}
void QtSqlDatabaseSession::requestCountRows(QString table)
{ QMetaObject::invokeMethod(m_worker, "countRows", Qt::QueuedConnection, Q_ARG(QString, std::move(table)), Q_ARG(quint64, m_queryControl->generation.load())); }
void QtSqlDatabaseSession::requestQuery(QString sql, bool allowScript)
{ QMetaObject::invokeMethod(m_worker, "executeQuery", Qt::QueuedConnection, Q_ARG(QString, std::move(sql)), Q_ARG(bool, allowScript), Q_ARG(quint64, m_queryControl->generation.load())); }
void QtSqlDatabaseSession::requestBackup(QString path)
{ QMetaObject::invokeMethod(m_worker, "backup", Qt::QueuedConnection, Q_ARG(QString, std::move(path))); }
void QtSqlDatabaseSession::requestCommit(PendingChanges changes)
{ QMetaObject::invokeMethod(m_worker, "commit", Qt::QueuedConnection, Q_ARG(PendingChanges, std::move(changes))); }
void QtSqlDatabaseSession::cancelQuery() { ++m_queryControl->generation; }
void QtSqlDatabaseSession::close() {
    cancelQuery();
    if (m_worker) QMetaObject::invokeMethod(m_worker, "close", Qt::QueuedConnection);
}

} // namespace devhub::infrastructure::database
