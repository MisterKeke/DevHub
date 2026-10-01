#pragma once

#include <QObject>
#include <QString>

class QElapsedTimer;
class QWidget;

namespace devhub::application::authentication {
class AuthenticationService;
class SessionController;
}
namespace devhub::presentation::theme { class ThemeManager; }

namespace devhub::presentation::authentication {

class AuthenticationWindow final : public QObject
{
    Q_OBJECT
public:
    AuthenticationWindow(bool accountExists,
                         QString storedUsername,
                         application::authentication::AuthenticationService& authentication,
                         application::authentication::SessionController& session,
                         QWidget* parent = nullptr,
                         QElapsedTimer* startupTimer = nullptr,
                         bool keepSessionDefault = false,
                         theme::ThemeManager* themeManager = nullptr);
    [[nodiscard]] int exec();

private:
    class Flow;
    application::authentication::AuthenticationService& m_authentication;
    application::authentication::SessionController& m_session;
    QString m_username;
    bool m_accountExists = false;
    bool m_keepSessionDefault = false;
    QElapsedTimer* m_startupTimer = nullptr;
    theme::ThemeManager* m_themeManager = nullptr;
};

} // namespace devhub::presentation::authentication
