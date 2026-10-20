#pragma once

#include "TaskTypes.h"
#include "../../shared/common/Result.h"
#include "../../shared/identifiers/IdGenerator.h"

#include <QHash>
#include <QSet>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QThreadPool>
#include <QThread>

#include <functional>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace devhub::application::tasks {

using shared::common::TaskOutcome;

struct TaskHandle final
{
    TaskId id;
    CancellationSource cancellation;

    void cancel() const noexcept { cancellation.requestCancellation(); }
};

class TaskScheduler final : public QObject
{
    Q_OBJECT

public:
    explicit TaskScheduler(shared::identifiers::IIdGenerator& idGenerator,
                           int maximumThreadCount = 0,
                           QObject* parent = nullptr);
    ~TaskScheduler() override;

    void setModuleConcurrencyLimit(QString moduleKey, int limit);
    void setGlobalConcurrencyLimit(int limit);
    void stopAcceptingAndCancel();
    void cancelAll();
    void cancelWorkspace(const QUuid& workspaceId);
    void setWorkspaceBlocked(const QUuid& workspaceId, bool blocked);
    [[nodiscard]] bool isAccepting() const noexcept { return m_accepting; }
    [[nodiscard]] int activeTaskCount() const noexcept { return static_cast<int>(m_runningCancellation.size()); }

    template<typename T, typename Work, typename Completion>
    [[nodiscard]] TaskHandle submit(TaskRequest request, Work&& work, Completion&& completion, int priority = 0)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        const TaskId taskId = m_idGenerator.create();
        if (request.moduleKey.isEmpty()) {
            request.moduleKey = QStringLiteral("general");
        }
        if (request.correlationId.isNull()) {
            request.correlationId.value = m_idGenerator.create();
        }

        CancellationSource cancellation;
        TaskHandle handle{taskId, cancellation};
        auto completionFunction = std::function<void(TaskOutcome<T>)>(std::forward<Completion>(completion));
        const bool workspaceBlocked = request.workspaceId && m_blockedWorkspaces.contains(*request.workspaceId);
        if (!m_accepting || workspaceBlocked) {
            const QString userMessage = workspaceBlocked
                ? QStringLiteral("This workspace is being removed and cannot start new work.")
                : QStringLiteral("DevHub is shutting down and cannot start new work.");
            auto error = domain::errors::AppError::failure(
                domain::errors::ErrorCode::ShutdownInProgress,
                domain::errors::ErrorCategory::Internal,
                userMessage,
                QStringLiteral("Task rejected because its workspace is blocked or the scheduler is shutting down."),
                QStringLiteral("schedule task"));
            QTimer::singleShot(0, this, [callback = std::move(completionFunction), error = std::move(error)]() mutable {
                callback(TaskOutcome<T>{shared::common::Result<T>::failure(std::move(error))});
            });
            return handle;
        }

        QPointer<TaskScheduler> scheduler(this);
        TaskContext context{taskId, request, request.correlationId, cancellation.token(), {}};
        context.reportProgress = [scheduler, taskId, request](QString message, int percentage) mutable {
            if (!scheduler) {
                return;
            }
            TaskProgress progress;
            progress.taskId = taskId;
            progress.correlationId = request.correlationId;
            progress.moduleKey = request.moduleKey;
            progress.message = std::move(message);
            progress.percentage = std::clamp(percentage, -1, 100);
            progress.workspaceId = request.workspaceId;
            progress.generation = request.generation;
            scheduler->reportProgress(std::move(progress));
        };
        auto workerFunction = std::function<shared::common::Result<T>(const TaskContext&)>(std::forward<Work>(work));
        auto execute = [scheduler,
                        context,
                        cancellation,
                        workerFunction = std::move(workerFunction),
                        completionFunction = std::move(completionFunction)]() mutable {
            TaskOutcome<T> outcome{shared::common::OperationCancelled{}};
            if (!cancellation.isCancellationRequested()) {
                try {
                    auto result = workerFunction(context);
                    if (cancellation.isCancellationRequested()) {
                        outcome = shared::common::OperationCancelled{};
                    } else {
                        outcome = std::move(result);
                    }
                } catch (const std::exception& exception) {
                    auto error = domain::errors::AppError::failure(
                        domain::errors::ErrorCode::InternalFailure,
                        domain::errors::ErrorCategory::Internal,
                        QStringLiteral("DevHub could not complete a background operation."),
                        QString::fromUtf8(exception.what()),
                        QStringLiteral("run background task"));
                    error.correlationId = context.correlationId;
                    outcome = shared::common::Result<T>::failure(std::move(error));
                } catch (...) {
                    auto error = domain::errors::AppError::failure(
                        domain::errors::ErrorCode::InternalFailure,
                        domain::errors::ErrorCategory::Internal,
                        QStringLiteral("DevHub could not complete a background operation."),
                        QStringLiteral("An unrecognized exception escaped a background task."),
                        QStringLiteral("run background task"));
                    error.correlationId = context.correlationId;
                    outcome = shared::common::Result<T>::failure(std::move(error));
                }
            }

            auto sharedOutcome = std::make_shared<TaskOutcome<T>>(std::move(outcome));
            if (scheduler) {
                QMetaObject::invokeMethod(scheduler, [scheduler,
                                                      context,
                                                      callback = std::move(completionFunction),
                                                      sharedOutcome]() mutable {
                    if (scheduler) {
                        scheduler->complete(context, std::move(callback), std::move(*sharedOutcome));
                    }
                }, Qt::QueuedConnection);
            }
        };

        enqueue(taskId, request, cancellation, std::move(execute), priority);
        return handle;
    }

    void reportProgress(TaskProgress progress);

signals:
    void progressChanged(TaskProgress progress);

private:
    struct PendingTask final
    {
        TaskId id;
        TaskRequest request;
        CancellationSource cancellation;
        std::function<void()> execute;
        int priority = 0;
        quint64 insertionOrder = 0;
    };

    void enqueue(TaskId id,
                 TaskRequest request,
                 CancellationSource cancellation,
                 std::function<void()> execute,
                 int priority);
    void dispatchPending();
    void completeTaskSlot(const TaskId& id, const QString& moduleKey);
    [[nodiscard]] bool isLatestGeneration(const TaskContext& context) const;

    template<typename T>
    void complete(TaskContext context,
                  std::function<void(TaskOutcome<T>)> callback,
                  TaskOutcome<T> outcome)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        const bool current = isLatestGeneration(context);
        completeTaskSlot(context.taskId, context.request.moduleKey);
        if (current) {
            callback(std::move(outcome));
        }
        dispatchPending();
    }

    shared::identifiers::IIdGenerator& m_idGenerator;
    QThreadPool m_threadPool;
    QVector<PendingTask> m_pending;
    QHash<QString, int> m_moduleConcurrencyLimits;
    QHash<QString, int> m_activeByModule;
    QHash<TaskId, CancellationSource> m_runningCancellation;
    QHash<TaskId, TaskRequest> m_requests;
    QSet<QUuid> m_blockedWorkspaces;
    QHash<QString, TaskGeneration> m_latestGenerationByOwner;
    quint64 m_nextInsertionOrder = 0;
    bool m_accepting = true;
};

} // namespace devhub::application::tasks
