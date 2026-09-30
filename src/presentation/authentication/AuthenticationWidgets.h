#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace devhub::presentation::authentication {

class BrandHeaderWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit BrandHeaderWidget(QString title, QString subtitle, QWidget* parent = nullptr);
};

class CreateAccountWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit CreateAccountWidget(QWidget* parent = nullptr);
    [[nodiscard]] QString username() const;
    [[nodiscard]] QString password() const;
    [[nodiscard]] QString confirmation() const;
    void clearPasswords();
    void setBusy(bool busy);
    void showMessage(const QString& message);
    void focusUsername();

signals:
    void createRequested();

private:
    QLineEdit* m_username = nullptr;
    QLineEdit* m_password = nullptr;
    QLineEdit* m_confirmation = nullptr;
    QPushButton* m_createButton = nullptr;
    QLabel* m_message = nullptr;
};

class SignInWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit SignInWidget(QString username, QWidget* parent = nullptr, bool keepSessionDefault = false);
    [[nodiscard]] QString username() const;
    [[nodiscard]] QString password() const;
    [[nodiscard]] bool keepSession() const;
    void clearPassword();
    void setBusy(bool busy);
    void setRetryRemaining(int milliseconds);
    void showMessage(const QString& message);
    void focusInitialField(bool usernameIsValid);

signals:
    void signInRequested();
    void forgotPasswordRequested();

private:
    QLineEdit* m_username = nullptr;
    QLineEdit* m_password = nullptr;
    QCheckBox* m_keepSession = nullptr;
    QPushButton* m_signInButton = nullptr;
    QLabel* m_message = nullptr;
    bool m_busy = false;
    int m_retryRemainingMs = 0;
};

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
