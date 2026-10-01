#include "AuthenticationWindow.h"

#include "../../application/authentication/AuthenticationService.h"
#include "../../application/authentication/SessionController.h"
#include "../theme/ThemeManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QDebug>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace devhub::presentation::authentication {

class AuthenticationWindow::Flow final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool accountExists READ accountExists NOTIFY accountModeChanged)
    Q_PROPERTY(QString username READ username NOTIFY usernameChanged)
    Q_PROPERTY(bool keepSessionDefault READ keepSessionDefault CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int retryRemaining READ retryRemaining NOTIFY retryRemainingChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    Flow(bool exists, QString user, bool keep, application::authentication::AuthenticationService& auth,
         application::authentication::SessionController& session)
        : m_accountExists(exists), m_username(std::move(user)), m_keepSessionDefault(keep),
          m_auth(auth), m_session(session)
    {
        connect(&m_auth, &application::authentication::AuthenticationService::busyChanged,
                this, [this](bool value) { m_busy = value; emit busyChanged(); });
        connect(&m_auth, &application::authentication::AuthenticationService::retryDelayChanged,
                this, [this](int value) { m_retryRemaining = value; emit retryRemainingChanged(); });
        m_busy = m_auth.isBusy();
        m_retryRemaining = m_auth.retryRemainingMs();
    }

    bool accountExists() const { return m_accountExists; }
    QString username() const { return m_username; }
    bool keepSessionDefault() const { return m_keepSessionDefault; }
    bool busy() const { return m_busy; }
    int retryRemaining() const { return m_retryRemaining; }

    Q_INVOKABLE void createAccount(const QString& username, const QString& password, const QString& confirmation)
    {
        if (m_busy) return;
        m_message.clear();
        emit messageChanged();
        m_session.beginAccountCreation();
        QPointer<Flow> self(this);
        m_auth.createAccount(username, password, confirmation,
            [self](shared::common::Result<domain::account::Account> result) mutable {
                if (!self) return;
                if (!result) {
                    self->m_session.accountCreationFailed();
                    self->m_message = result.error().userMessage;
                    emit self->messageChanged();
                    return;
                }
                self->m_session.accountCreated(std::move(result).value());
                emit self->finished(true);
            });
    }

    Q_INVOKABLE void signIn(const QString& username, const QString& password, bool keepSession)
    {
        if (m_busy) return;
        m_message.clear();
        emit messageChanged();
        m_session.beginSignIn();
        QPointer<Flow> self(this);
        m_auth.signIn(username, password,
            [self, keepSession](shared::common::Result<domain::account::Account> result) mutable {
                if (!self) return;
                if (!result) {
                    self->m_session.signInFailed();
                    self->m_message = result.error().userMessage;
                    emit self->messageChanged();
                    return;
                }
                self->m_session.signInSucceeded(std::move(result).value(), keepSession);
                emit self->finished(true);
            });
    }

    Q_INVOKABLE void resetProfile(const QString& confirmation)
    {
        if (m_busy) return;
        m_message.clear();
        emit messageChanged();
        QPointer<Flow> self(this);
        m_auth.resetProfile(confirmation,
            [self](shared::common::Result<void> result) mutable {
                if (!self) return;
                if (!result) {
                    self->m_message = result.error().userMessage;
                    emit self->messageChanged();
                    return;
                }
                self->m_session.profileReset();
                self->m_accountExists = false;
                self->m_username.clear();
                emit self->accountModeChanged();
                emit self->usernameChanged();
            });
    }

    Q_INVOKABLE void quit() { emit finished(false); }
    Q_INVOKABLE QString message() const { return m_message; }
    Q_INVOKABLE void clearMessage()
    {
        if (m_message.isEmpty()) return;
        m_message.clear();
        emit messageChanged();
    }

signals:
    void accountModeChanged();
    void usernameChanged();
    void busyChanged();
    void retryRemainingChanged();
    void messageChanged();
    void finished(bool accepted);

private:
    bool m_accountExists = false;
    QString m_username;
    bool m_keepSessionDefault = false;
    bool m_busy = false;
    int m_retryRemaining = 0;
    QString m_message;
    application::authentication::AuthenticationService& m_auth;
    application::authentication::SessionController& m_session;
};

AuthenticationWindow::AuthenticationWindow(bool accountExists,
                                           QString storedUsername,
                                           application::authentication::AuthenticationService& authentication,
                                           application::authentication::SessionController& session,
                                           QWidget* parent,
                                           QElapsedTimer* startupTimer,
                                           bool keepSessionDefault,
                                           theme::ThemeManager* themeManager)
    : QObject(parent), m_authentication(authentication), m_session(session),
      m_username(std::move(storedUsername)), m_accountExists(accountExists),
      m_keepSessionDefault(keepSessionDefault), m_startupTimer(startupTimer),
      m_themeManager(themeManager)
{
}

int AuthenticationWindow::exec()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    Flow flow(m_accountExists, m_username, m_keepSessionDefault, m_authentication, m_session);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("authFlow"), &flow);
    engine.rootContext()->setContextProperty(QStringLiteral("themeController"), m_themeManager);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/DevHub/Authentication.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Unable to load the QML authentication window";
        return 0;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (!window) {
        qCritical() << "The QML authentication root is not a window";
        return 0;
    }
    window->setModality(Qt::ApplicationModal);
    bool accepted = false;
    QEventLoop loop;
    connect(&flow, &Flow::finished, &loop, [&loop, &accepted, window](bool result) {
        accepted = result;
        window->close();
        loop.quit();
    });
    connect(window, &QQuickWindow::visibleChanged, &loop, [this, window] {
        if (window->isVisible() && m_startupTimer != nullptr) {
            qInfo().noquote() << QStringLiteral("DevHub startup to authentication screen: %1 ms")
                                     .arg(m_startupTimer->elapsed());
            m_startupTimer = nullptr;
        }
    });
    connect(window, &QQuickWindow::closing, &loop, [&loop] { loop.quit(); });
    QTimer::singleShot(0, window, &QQuickWindow::show);
    const QString capturePrefix = QStringLiteral("--capture-auth=");
    for (const QString& argument : QCoreApplication::arguments()) {
        if (!argument.startsWith(capturePrefix)) continue;
        const QString outputPath = argument.mid(capturePrefix.size());
        QTimer::singleShot(1200, window, [window, outputPath, &flow] {
            if (!window->grabWindow().save(outputPath)) {
                qCritical().noquote() << QStringLiteral("Unable to save authentication capture: %1").arg(outputPath);
            }
            flow.quit();
        });
        break;
    }
    loop.exec();
    return accepted ? 1 : 0;
}

} // namespace devhub::presentation::authentication

#include "AuthenticationWindow.moc"
