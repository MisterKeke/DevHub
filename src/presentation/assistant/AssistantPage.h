#pragma once
#include "application/assistant/AssistantController.h"
#include <QWidget>
#include <QHash>
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QCheckBox;
class QLineEdit;
namespace devhub::presentation::widgets { class SafeMarkdownBrowser; }
namespace devhub::application::assistant { class AssistantToolRuntime; }
namespace devhub::application::settings { class SettingsService; }
namespace devhub::presentation::assistant {
class AssistantPage final : public QWidget {
    Q_OBJECT
public:
    explicit AssistantPage(application::assistant::AssistantController&, QWidget* parent = nullptr);
    explicit AssistantPage(application::assistant::AssistantController&,
        application::assistant::AssistantToolRuntime&, application::settings::SettingsService&,
        QWidget* parent = nullptr);
    void activate();
    void clearDraft();
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    AssistantPage(application::assistant::AssistantController&,
        application::assistant::AssistantToolRuntime*, application::settings::SettingsService*,
        QWidget* parent);
    struct Card { QWidget* widget; widgets::SafeMarkdownBrowser* body; QLabel* status; QPushButton* retry; };
    void refreshState();
    void refreshSomethingTasks();
    void refreshApprovals();
    void addMessage(const QUuid&);
    void updateMessage(const QUuid&);
    void sendDraft();
    bool shouldFollow() const;
    void restoreScroll(bool follow, int previous);
    application::assistant::AssistantController& m_controller;
    application::assistant::AssistantToolRuntime* m_toolRuntime = nullptr;
    application::settings::SettingsService* m_settings = nullptr;
    QHash<QUuid, Card> m_cards;
    QLabel* m_title;
    QLabel* m_status;
    QLabel* m_context;
    QLabel* m_toolActivity;
    QCheckBox* m_allowNotes;
    QCheckBox* m_allowFiles;
    QCheckBox* m_allowTaskData;
    QLineEdit* m_somethingProfilePath;
    QLabel* m_somethingProfileIdentity;
    QLabel* m_somethingConnectionStatus;
    QLabel* m_somethingLookupStatus;
    QLineEdit* m_workspaceTaskTag;
    QCheckBox* m_restrictWorkspaceTaskTag;
    QPushButton* m_chooseSomethingProfile;
    QPushButton* m_checkSomethingConnection;
    QLabel* m_error;
    QPlainTextEdit* m_composer;
    QPushButton* m_send;
    QPushButton* m_stop;
    QPushButton* m_new;
    QPushButton* m_reconnect;
    QPushButton* m_latest;
    QScrollArea* m_scroll;
    QVBoxLayout* m_messages;
    QVBoxLayout* m_approvalCards;
    QScrollArea* m_approvalScroll;
    bool m_preedit = false;
    bool m_followLatest = true;
    bool m_rendering = false;
    bool m_adjustingScroll = false;
    quint64 m_scrollGeneration = 0;
    quint64 m_scrollRevision = 0;
    QStringList m_activeTools;
};
}
