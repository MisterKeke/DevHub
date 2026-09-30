#pragma once
#include "../../application/tasks/TaskScheduler.h"
#include "../../application/git/GitTypes.h"
#include <QWidget>
#include <QUuid>
#include <optional>
#include <QVector>

class QLabel; class QListWidget; class QPushButton; class QPlainTextEdit; class QStackedWidget;
class QShowEvent;
namespace devhub::application::settings { class SettingsService; }
namespace devhub::presentation::git {
class GitPage final : public QWidget {
    Q_OBJECT
public:
    explicit GitPage(application::tasks::TaskScheduler& scheduler, QWidget* parent = nullptr,
                      application::settings::SettingsService* settings = nullptr);
    void setWorkspacePath(QString path, std::optional<QUuid> workspaceId = std::nullopt);
signals:
    void operationFailed(QString title, QString message);
    void overviewUpdated(QString summary, QStringList details, QString statusLabel, QString statusTone);
protected:
    void showEvent(QShowEvent* event) override;
private:
    void refresh();
    void loadOlderHistory();
    void appendCommit(const application::git::GitCommit& commit);
    void showDiff(QString commit = {}, bool staged = false);
    void setBusy(bool busy);
    void runMutation(QString operation, QStringList arguments);
    application::tasks::TaskScheduler& m_scheduler;
    application::settings::SettingsService* m_settings = nullptr;
    QString m_path;
    std::optional<QUuid> m_workspaceId;
    quint64 m_statusGeneration = 0;
    quint64 m_diffGeneration = 0;
    quint64 m_historyGeneration = 0;
    application::tasks::TaskHandle m_activeTask;
    application::tasks::TaskHandle m_diffTask;
    application::tasks::TaskHandle m_historyTask;
    QLabel* m_state = nullptr;
    QLabel* m_partialErrors = nullptr;
    QPushButton* m_refresh = nullptr;
    QPushButton* m_cancel = nullptr;
    QVector<QPushButton*> m_mutationButtons;
    QListWidget* m_status = nullptr;
    QListWidget* m_history = nullptr;
    QPushButton* m_loadMore = nullptr;
    QListWidget* m_branches = nullptr;
    QPlainTextEdit* m_diff = nullptr;
};
}
