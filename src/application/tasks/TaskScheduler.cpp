#include "TaskScheduler.h"

#include <QMetaObject>
#include <QThread>
#include <QRunnable>
#include <QTimer>

#include <algorithm>

namespace devhub::application::tasks {
namespace {

QString generationOwnerKey(const TaskRequest& request)
{
    if (!request.workspaceId.has_value()) {
        return {};
    }
    return request.workspaceId->toString(QUuid::WithoutBraces) + QStringLiteral("|") + request.moduleKey;
}

} // namespace

TaskScheduler::TaskScheduler(shared::identifiers::IIdGenerator& idGenerator,
                             int maximumThreadCount,
                             QObject* parent)
    : QObject(parent),
      m_idGenerator(idGenerator)
{
    const int idealCount = std::max(1, QThread::idealThreadCount());
    m_threadPool.setMaxThreadCount(maximumThreadCount > 0 ? maximumThreadCount : std::max(1, idealCount - 1));
    m_threadPool.setExpiryTimeout(30000);
    qRegisterMetaType<TaskProgress>();
}

TaskScheduler::~TaskScheduler()
{
    stopAcceptingAndCancel();
    m_threadPool.waitForDone();
}

void TaskScheduler::setModuleConcurrencyLimit(QString moduleKey, int limit)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (moduleKey.isEmpty() || limit < 1) {
        return;
    }
    m_moduleConcurrencyLimits.insert(std::move(moduleKey), limit);
    dispatchPending();
}

void TaskScheduler::setGlobalConcurrencyLimit(int limit)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_threadPool.setMaxThreadCount(std::max(1, limit));
    dispatchPending();
}

void TaskScheduler::stopAcceptingAndCancel()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_accepting = false;
    cancelAll();
}

void TaskScheduler::cancelAll()
{
    Q_ASSERT(QThread::currentThread() == thread());
    for (const CancellationSource& source : m_runningCancellation) {
        source.requestCancellation();
    }
    for (const PendingTask& task : m_pending) {
        task.cancellation.requestCancellation();
    }
}

void TaskScheduler::reportProgress(TaskProgress progress)
{
    if (QThread::currentThread() == thread()) {
        emit progressChanged(std::move(progress));
        return;
    }
    QPointer<TaskScheduler> scheduler(this);
    QMetaObject::invokeMethod(this, [scheduler, progress = std::move(progress)]() mutable {
        if (scheduler) {
            emit scheduler->progressChanged(std::move(progress));
        }
    }, Qt::QueuedConnection);
}

void TaskScheduler::enqueue(TaskId id,
                            TaskRequest request,
                            CancellationSource cancellation,
                            std::function<void()> execute,
                            int priority)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QString ownerKey = generationOwnerKey(request);
    if (!ownerKey.isEmpty()) {
        const auto current = m_latestGenerationByOwner.constFind(ownerKey);
        if (current == m_latestGenerationByOwner.cend() || request.generation > *current) {
            m_latestGenerationByOwner.insert(ownerKey, request.generation);
        }
    }

    m_pending.append(PendingTask{id,
                                std::move(request),
                                std::move(cancellation),
                                std::move(execute),
                                priority,
                                m_nextInsertionOrder++});
    dispatchPending();
}

void TaskScheduler::dispatchPending()
{
    Q_ASSERT(QThread::currentThread() == thread());
    while (!m_pending.isEmpty() && m_runningCancellation.size() < m_threadPool.maxThreadCount()) {
        std::stable_sort(m_pending.begin(), m_pending.end(), [](const PendingTask& lhs, const PendingTask& rhs) {
            if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
            }
            return lhs.insertionOrder < rhs.insertionOrder;
        });

        auto available = std::find_if(m_pending.begin(), m_pending.end(), [this](const PendingTask& task) {
            const int limit = m_moduleConcurrencyLimits.value(task.request.moduleKey,
                                                              m_threadPool.maxThreadCount());
            return m_activeByModule.value(task.request.moduleKey) < limit;
        });
        if (available == m_pending.end()) {
            return;
        }

        PendingTask task = std::move(*available);
        m_pending.erase(available);
        m_activeByModule[task.request.moduleKey] += 1;
        m_runningCancellation.insert(task.id, task.cancellation);
        auto execute = std::move(task.execute);
        auto* runnable = QRunnable::create([execute = std::move(execute)]() mutable { execute(); });
        m_threadPool.start(runnable, task.priority);
    }
}

void TaskScheduler::completeTaskSlot(const TaskId& id, const QString& moduleKey)
{
    m_runningCancellation.remove(id);
    const int activeCount = m_activeByModule.value(moduleKey);
    if (activeCount <= 1) {
        m_activeByModule.remove(moduleKey);
    } else {
        m_activeByModule.insert(moduleKey, activeCount - 1);
    }
}

bool TaskScheduler::isLatestGeneration(const TaskContext& context) const
{
    const QString ownerKey = generationOwnerKey(context.request);
    if (ownerKey.isEmpty()) {
        return true;
    }
    const auto latest = m_latestGenerationByOwner.constFind(ownerKey);
    return latest != m_latestGenerationByOwner.cend() && *latest == context.request.generation;
}

} // namespace devhub::application::tasks
