#pragma once

#include <QDialog>

class QStackedWidget;
class QElapsedTimer;
class QShowEvent;

namespace devhub::application::authentication {
class AuthenticationService;
class SessionController;
}

namespace devhub::presentation::authentication {

class CreateAccountWidget;
class SignInWidget;

class AuthenticationWindow final : public QDialog
{
    Q_OBJECT
public:
    AuthenticationWindow(bool accountExists,
                         QString storedUsername,
                         application::authentication::AuthenticationService& authentication,
                         application::authentication::SessionController& session,
                         QWidget* parent = nullptr,
                         QElapsedTimer* startupTimer = nullptr,
                         bool keepSessionDefault = false);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void submitCreateAccount();
    void submitSignIn();
    void showForgotPasswordInformation();

    application::authentication::AuthenticationService& m_authentication;
    application::authentication::SessionController& m_session;
    CreateAccountWidget* m_createAccount = nullptr;
    SignInWidget* m_signIn = nullptr;
    QStackedWidget* m_pages = nullptr;
    QElapsedTimer* m_startupTimer = nullptr;
    bool m_startupTimeReported = false;
};

} // namespace devhub::presentation::authentication
