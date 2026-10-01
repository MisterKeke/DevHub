#include "../src/application/authentication/AuthenticationService.h"
#include "../src/application/authentication/AuthenticationValidation.h"
#include "../src/application/authentication/InactivityMonitor.h"
#include "../src/application/authentication/IMonotonicClock.h"
#include "../src/application/authentication/SessionController.h"
#include "../src/application/security/PasswordTaskService.h"
#include "../src/application/tasks/TaskScheduler.h"
#include "../src/domain/account/Account.h"
#include "../src/infrastructure/account/SqlAccountRepository.h"
#include "../src/infrastructure/account/SodiumPasswordHasher.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"
#include "../src/infrastructure/filesystem/LocalProfileResetter.h"
#include "../src/infrastructure/filesystem/LocalUserDirectoryProvisioner.h"
#include "../src/presentation/authentication/AuthenticationWidgets.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest/QtTest>

#include <optional>
#include <utility>

namespace {

template<typename T, typename Start>
devhub::shared::common::Result<T> awaitResult(Start start)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    std::optional<devhub::shared::common::Result<T>> result;
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    start([&](devhub::shared::common::Result<T> value) {
        result.emplace(std::move(value));
        loop.quit();
    });
    if (!result.has_value()) {
        timeout.start(30000);
        loop.exec();
    }
    if (!result.has_value()) {
        return devhub::shared::common::Result<T>::failure(devhub::domain::errors::AppError::failure(
            devhub::domain::errors::ErrorCode::InternalFailure,
            devhub::domain::errors::ErrorCategory::Internal,
            QStringLiteral("The authentication operation timed out."),
            QStringLiteral("The authentication integration test exceeded its completion timeout."),
            QStringLiteral("wait for authentication test")));
    }
    return std::move(*result);
}

struct TestServices final
{
    QTemporaryDir temporaryDirectory;
    QString localDataPath = temporaryDirectory.filePath(QStringLiteral("profile"));
    QString usersPath = QDir(localDataPath).filePath(QStringLiteral("users"));
    QString databasePath = QDir(localDataPath).filePath(QStringLiteral("data/devhub.db"));
    QString backupPath = QDir(localDataPath).filePath(QStringLiteral("backups"));
    devhub::infrastructure::database::InternalDatabaseManager database{databasePath, backupPath};
    devhub::shared::identifiers::UuidIdGenerator idGenerator;
    devhub::infrastructure::account::SqlAccountRepository repository{database, idGenerator};
    devhub::infrastructure::filesystem::LocalUserDirectoryProvisioner directoryProvisioner{usersPath};
    devhub::infrastructure::filesystem::LocalProfileResetter profileResetter{
        localDataPath, usersPath, database};
    devhub::infrastructure::account::SodiumPasswordHasher hasher{
        devhub::infrastructure::account::PasswordHashPolicy::fastTest()};
    devhub::application::tasks::TaskScheduler scheduler{idGenerator, 1};
    devhub::application::security::PasswordTaskService passwordTasks{scheduler, hasher};
    devhub::application::authentication::AuthenticationService authentication{
        repository, directoryProvisioner, profileResetter, idGenerator, passwordTasks};
};

class FakeMonotonicClock final : public devhub::application::authentication::IMonotonicClock
{
public:
    void restart() override { m_elapsedMs = 0; }
    [[nodiscard]] qint64 elapsedMs() const override { return m_elapsedMs; }
    void advance(qint64 milliseconds) { m_elapsedMs += milliseconds; }

private:
    qint64 m_elapsedMs = 0;
};

} // namespace

class AuthenticationTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(devhub::infrastructure::account::SodiumPasswordHasher::initialize());
    }

    void validatesRequiredInputsAndPasswordConfirmation()
    {
        auto missing = devhub::application::authentication::validateCreateAccountInput(
            QStringLiteral("username"), {}, {});
        QVERIFY(!missing.isValid());
        auto mismatch = devhub::application::authentication::validateCreateAccountInput(
            QStringLiteral("username"), QStringLiteral("first"), QStringLiteral("second"));
        QVERIFY(!mismatch.isValid());
        auto accepted = devhub::application::authentication::validateCreateAccountInput(
            QStringLiteral("  Local User  "), QStringLiteral("pass phrase"), QStringLiteral("pass phrase"));
        QVERIFY(accepted.isValid());
        QCOMPARE(accepted.displayUsername->value(), QStringLiteral("Local User"));
        QVERIFY(devhub::application::authentication::validateSignInInput(
            QStringLiteral("local user"), QStringLiteral("password")).isValid());
        QVERIFY(!devhub::application::authentication::validateUnlockInput({}).valid);
    }

    void accountSetupSignInPasswordChangeAndProfileReset()
    {
        TestServices services;
        QVERIFY(services.temporaryDirectory.isValid());
        QVERIFY(services.database.open());
        QVERIFY(services.database.migrate());

        auto created = awaitResult<devhub::domain::account::Account>([&](auto completion) {
            services.authentication.createAccount(QStringLiteral("Local User"),
                                                   QStringLiteral("first password"),
                                                   QStringLiteral("first password"),
                                                   std::move(completion));
        });
        QVERIFY(created);
        const QString userPath = QDir(services.usersPath).filePath(created.value().id().toString());
        QVERIFY(QDir(QDir(userPath).filePath(QStringLiteral("notes"))).exists());
        QVERIFY(QDir(QDir(userPath).filePath(QStringLiteral("recovery"))).exists());
        QVERIFY(QDir(QDir(userPath).filePath(QStringLiteral("cache"))).exists());

        auto wrongPassword = awaitResult<devhub::domain::account::Account>([&](auto completion) {
            services.authentication.signIn(QStringLiteral("different name"),
                                            QStringLiteral("bad password"),
                                            std::move(completion));
        });
        QVERIFY(!wrongPassword);
        QCOMPARE(wrongPassword.error().userMessage, QStringLiteral("Username or password is incorrect."));

        auto changed = awaitResult<void>([&](auto completion) {
            services.authentication.changePassword(
                created.value().normalizedUsername(), QStringLiteral("first password"),
                QStringLiteral("second password"), QStringLiteral("second password"),
                std::move(completion));
        });
        QVERIFY(changed);

        auto signedIn = awaitResult<devhub::domain::account::Account>([&](auto completion) {
            services.authentication.signIn(QStringLiteral("LOCAL USER"),
                                            QStringLiteral("second password"),
                                            std::move(completion));
        });
        QVERIFY(signedIn);
        QCOMPARE(signedIn.value().id(), created.value().id());

        devhub::application::authentication::SessionController session;
        session.accountAvailable();
        session.beginSignIn();
        session.signInSucceeded(signedIn.value(), false);
        FakeMonotonicClock fakeClock;
        devhub::application::authentication::InactivityMonitor inactivityMonitor(
            *QCoreApplication::instance(), session, fakeClock, 1000);
        fakeClock.advance(999);
        inactivityMonitor.checkTimeout();
        QCOMPARE(session.state(), devhub::application::authentication::SessionState::SignedIn);
        fakeClock.advance(1);
        inactivityMonitor.checkTimeout();
        QCOMPARE(session.state(), devhub::application::authentication::SessionState::Locked);
        session.beginUnlock();
        session.unlockSucceeded(signedIn.value());
        QCOMPARE(session.state(), devhub::application::authentication::SessionState::SignedIn);
        session.signOut();
        QCOMPARE(session.state(), devhub::application::authentication::SessionState::SignedOut);
        QVERIFY(!session.account().has_value());
        QVERIFY(!session.keepSession());
        session.beginSignIn();
        session.signInSucceeded(signedIn.value(), true);
        QVERIFY(session.keepSession());
        session.beginLock();
        QCOMPARE(session.state(), devhub::application::authentication::SessionState::Locked);
        session.signOut();

        const QString workspacePath = services.temporaryDirectory.filePath(QStringLiteral("external-workspace"));
        QVERIFY(QDir().mkpath(workspacePath));
        QFile workspaceMarker(QDir(workspacePath).filePath(QStringLiteral("keep.txt")));
        QVERIFY(workspaceMarker.open(QIODevice::WriteOnly));
        workspaceMarker.write("preserve");
        workspaceMarker.close();

        auto reset = awaitResult<void>([&](auto completion) {
            services.authentication.resetProfile(QStringLiteral("RESET"), std::move(completion));
        });
        QVERIFY(reset);
        auto noAccount = services.repository.findSingleAccount();
        QVERIFY(noAccount);
        QVERIFY(!noAccount.value().has_value());
        QVERIFY(QFileInfo::exists(QDir(workspacePath).filePath(QStringLiteral("keep.txt"))));
        const QStringList recoveryProfiles = QDir(QDir(services.localDataPath).filePath(QStringLiteral("recovery")))
                                                 .entryList({QStringLiteral("profile-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(recoveryProfiles.size(), 1);
        const QString recoveryProfile = QDir(QDir(services.localDataPath).filePath(QStringLiteral("recovery")))
                                            .filePath(recoveryProfiles.first());
        QVERIFY(QFileInfo::exists(QDir(recoveryProfile).filePath(QStringLiteral("devhub.db"))));
        QVERIFY(QDir(QDir(recoveryProfile).filePath(QStringLiteral("user-%1").arg(created.value().id().toString())))
                    .exists());
    }

    void lockedWorkspaceKeepsTheAccountIdentityAndPasswordControl()
    {
        devhub::presentation::authentication::UnlockWidget unlock(QStringLiteral("Local User"));
        QCOMPARE(unlock.findChildren<QLineEdit*>().size(), 1);
        QVERIFY(unlock.findChild<QLabel*>(QStringLiteral("lockedAccountIdentity")) != nullptr);
    }
};

QTEST_MAIN(AuthenticationTest)
#include "authentication_test.moc"
