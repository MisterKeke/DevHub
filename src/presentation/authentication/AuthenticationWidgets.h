#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace devhub::presentation::authentication {

class UnlockWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit UnlockWidget(QString username, QWidget* parent = nullptr);
    void setUsername(QString username);
    [[nodiscard]] QString password() const;
    void clearPassword();
    void setBusy(bool busy);
    void setRetryRemaining(int milliseconds);
    void showMessage(const QString& message);
    void focusPassword();

signals:
    void unlockRequested();
    void signOutRequested();
    void quitRequested();

private:
    QLineEdit* m_password = nullptr;
    QPushButton* m_unlockButton = nullptr;
    QLabel* m_message = nullptr;
    QLabel* m_identity = nullptr;
    bool m_busy = false;
    int m_retryRemainingMs = 0;
};

} // namespace devhub::presentation::authentication
