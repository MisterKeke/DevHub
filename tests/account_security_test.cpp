#include "../src/application/security/PasswordHasher.h"
#include "../src/domain/account/Username.h"
#include "../src/application/security/PasswordTaskService.h"
#include "../src/application/security/SecurePasswordBuffer.h"
#include "../src/infrastructure/account/SqlAccountRepository.h"
#include "../src/infrastructure/account/SodiumPasswordHasher.h"
#include "../src/infrastructure/database/InternalDatabaseManager.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#include "../src/application/tasks/TaskScheduler.h"
#include "../src/shared/identifiers/IdGenerator.h"

#include <optional>
#include <utility>

namespace {

template<typename Function>
auto executeInWorker(Function function)
{
    using ReturnType = decltype(function());
    std::optional<ReturnType> result;
    QThread* worker = QThread::create([&result, function = std::move(function)]() mutable {
        result.emplace(function());
    });
    worker->start();
    worker->wait();
    delete worker;
    return std::move(*result);
}

devhub::shared::common::Result<devhub::domain::account::PasswordVerifier>
hashInWorker(devhub::infrastructure::account::SodiumPasswordHasher& hasher, QString password)
{
    devhub::application::security::SecurePasswordBuffer buffer(std::move(password));
    return executeInWorker([&hasher, &buffer] {
        auto result = hasher.hash(buffer);
        buffer.clear();
        return result;
    });
}

devhub::shared::common::Result<bool> verifyInWorker(
    devhub::infrastructure::account::SodiumPasswordHasher& hasher,
    QString password,
    const devhub::domain::account::PasswordVerifier& verifier)
{
    devhub::application::security::SecurePasswordBuffer buffer(std::move(password));
    return executeInWorker([&hasher, &buffer, &verifier] {
        auto result = hasher.verify(buffer, verifier);
        buffer.clear();
        return result;
    });
}

devhub::shared::common::Result<bool> needsRehashInWorker(
    devhub::infrastructure::account::SodiumPasswordHasher& hasher,
    const devhub::domain::account::PasswordVerifier& verifier)
{
    return executeInWorker([&hasher, &verifier] { return hasher.needsRehash(verifier); });
}

} // namespace

class AccountSecurityTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(devhub::infrastructure::account::SodiumPasswordHasher::initialize());
    }

    void hashesAndVerifiesKnownPassword()
    {
        devhub::infrastructure::account::SodiumPasswordHasher hasher(
            devhub::infrastructure::account::PasswordHashPolicy::fastTest());
        auto verifier = hashInWorker(hasher, QStringLiteral("known test password"));
        QVERIFY(verifier);
        auto verified = verifyInWorker(hasher, QStringLiteral("known test password"), verifier.value());
        QVERIFY(verified);
        QVERIFY(verified.value());
    }

    void rejectsWrongPasswordWithoutAnInternalError()
    {
        devhub::infrastructure::account::SodiumPasswordHasher hasher(
            devhub::infrastructure::account::PasswordHashPolicy::fastTest());
        auto verifier = hashInWorker(hasher, QStringLiteral("correct password"));
        QVERIFY(verifier);
        auto verified = verifyInWorker(hasher, QStringLiteral("different password"), verifier.value());
        QVERIFY(verified);
        QVERIFY(!verified.value());
    }

    void malformedStoredVerifierIsReportedWithoutEchoingIt()
    {
        devhub::infrastructure::account::SodiumPasswordHasher hasher(
            devhub::infrastructure::account::PasswordHashPolicy::fastTest());
        auto verifier = devhub::domain::account::PasswordVerifier::fromStorage(
            QStringLiteral("malformed-verifier"), 1);
        QVERIFY(verifier);
        auto needsRehash = needsRehashInWorker(hasher, verifier.value());
        QVERIFY(!needsRehash);
        QCOMPARE(needsRehash.error().code, devhub::domain::errors::ErrorCode::InvalidPasswordHash);
        QVERIFY(!needsRehash.error().technicalMessage.contains(QStringLiteral("malformed-verifier")));
    }

    void reportsWhenParametersNeedRehashing()
    {
        const auto fastPolicy = devhub::infrastructure::account::PasswordHashPolicy::fastTest();
        devhub::infrastructure::account::SodiumPasswordHasher fastHasher(fastPolicy);
        auto verifier = hashInWorker(fastHasher, QStringLiteral("rehash fixture password"));
        QVERIFY(verifier);
        auto strongerPolicy = fastPolicy;
        strongerPolicy.memoryLimitBytes *= 2U;
        devhub::infrastructure::account::SodiumPasswordHasher strongerHasher(strongerPolicy);
        auto needsRehash = needsRehashInWorker(strongerHasher, verifier.value());
        QVERIFY(needsRehash);
        QVERIFY(needsRehash.value());
    }

    void trimsAndCaseFoldsUnicodeUsernames()
    {
        auto display = devhub::domain::account::DisplayUsername::create(QStringLiteral("  Straße  "));
        QVERIFY(display);
        QCOMPARE(display.value().value(), QStringLiteral("Straße"));
        auto normalized = devhub::domain::account::NormalizedUsername::fromDisplay(display.value());
        auto equivalent = devhub::domain::account::NormalizedUsername::create(QStringLiteral("STRASSE"));
        auto uppercaseSharpS = devhub::domain::account::NormalizedUsername::create(QStringLiteral("STRAẞE"));
        QVERIFY(normalized);
        QVERIFY(equivalent);
        QVERIFY(uppercaseSharpS);
        QCOMPARE(normalized.value(), equivalent.value());
        QCOMPARE(normalized.value(), uppercaseSharpS.value());
    }

    void rejectsInvalidUsernameCharactersAndLengths()
    {
        QVERIFY(!devhub::domain::account::DisplayUsername::create(QStringLiteral(" ")));
        QVERIFY(!devhub::domain::account::DisplayUsername::create(QStringLiteral("has/path")));
        QVERIFY(!devhub::domain::account::DisplayUsername::create(QStringLiteral("has\\path")));
        QVERIFY(!devhub::domain::account::DisplayUsername::create(QStringLiteral("line\nbreak")));
        QVERIFY(!devhub::domain::account::DisplayUsername::create(QString(65, QLatin1Char('x'))));
        QVERIFY(!devhub::domain::account::NormalizedUsername::create(QString(QChar(u'\uFF3C'))));
    }

    void passwordTasksRunInBackgroundAndRejectDuplicates()
    {
        devhub::shared::identifiers::UuidIdGenerator idGenerator;
        devhub::application::tasks::TaskScheduler scheduler(idGenerator, 1);
        devhub::infrastructure::account::SodiumPasswordHasher hasher(
            devhub::infrastructure::account::PasswordHashPolicy::fastTest());
        devhub::application::security::PasswordTaskService service(scheduler, hasher);
        QEventLoop loop;
        int completions = 0;
        bool hashSucceeded = false;
        bool duplicateRejected = false;
        const auto finishOne = [&] {
            ++completions;
            if (completions == 2) {
                loop.quit();
            }
        };

        const auto first = service.hashPassword(QStringLiteral("async password"),
            [&hashSucceeded, &finishOne](auto outcome) {
                if (const auto* result = std::get_if<devhub::shared::common::Result<
                        devhub::domain::account::PasswordVerifier>>(&outcome)) {
                    hashSucceeded = result->hasValue();
                }
                finishOne();
            });
        QVERIFY(!first.id.isNull());
        QVERIFY(service.isBusy());
        const auto duplicate = service.hashPassword(QStringLiteral("duplicate password"),
            [&duplicateRejected, &finishOne](auto outcome) {
                if (const auto* result = std::get_if<devhub::shared::common::Result<
                        devhub::domain::account::PasswordVerifier>>(&outcome)) {
                    duplicateRejected = !result->hasValue()
                        && result->error().code == devhub::domain::errors::ErrorCode::PasswordOperationBusy;
                }
                finishOne();
            });
        QVERIFY(duplicate.id.isNull());
        loop.exec();
        QVERIFY(hashSucceeded);
        QVERIFY(duplicateRejected);
        QVERIFY(!service.isBusy());
    }

    void successfulVerificationRehashesAnOutdatedVerifier()
    {
        auto oldPolicy = devhub::infrastructure::account::PasswordHashPolicy::fastTest();
        devhub::infrastructure::account::SodiumPasswordHasher oldHasher(oldPolicy);
        auto verifier = hashInWorker(oldHasher, QStringLiteral("password to upgrade"));
        QVERIFY(verifier);

        auto currentPolicy = oldPolicy;
        currentPolicy.memoryLimitBytes *= 2U;
        devhub::infrastructure::account::SodiumPasswordHasher currentHasher(currentPolicy);
        devhub::shared::identifiers::UuidIdGenerator idGenerator;
        devhub::application::tasks::TaskScheduler scheduler(idGenerator, 1);
        devhub::application::security::PasswordTaskService service(scheduler, currentHasher);
        QEventLoop loop;
        bool verified = false;
        bool replacementCreated = false;
        const auto handle = service.verifyPassword(
            QStringLiteral("password to upgrade"), std::move(verifier).value(),
            [&loop, &verified, &replacementCreated](auto outcome) {
                if (const auto* result = std::get_if<devhub::shared::common::Result<
                        devhub::application::security::PasswordVerificationResult>>(&outcome)) {
                    if (result->hasValue()) {
                        verified = result->value().verified;
                        replacementCreated = result->value().replacementVerifier.has_value();
                    }
                }
                loop.quit();
            });
        QVERIFY(!handle.id.isNull());
        loop.exec();
        QVERIFY(verified);
        QVERIFY(replacementCreated);
    }

    void repositoryEnforcesOneAccountAndPersistsSuccessfulLogin()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        devhub::infrastructure::database::InternalDatabaseManager manager(
            temporaryDirectory.filePath(QStringLiteral("data/devhub.db")),
            temporaryDirectory.filePath(QStringLiteral("backups")));
        QVERIFY(manager.open());
        QVERIFY(manager.migrate());

        devhub::shared::identifiers::UuidIdGenerator idGenerator;
        devhub::infrastructure::account::SqlAccountRepository repository(manager, idGenerator);
        auto display = devhub::domain::account::DisplayUsername::create(QStringLiteral("Local User"));
        QVERIFY(display);
        auto normalized = devhub::domain::account::NormalizedUsername::fromDisplay(display.value());
        auto verifier = devhub::domain::account::PasswordVerifier::fromStorage(
            QStringLiteral("test-verifier-value"), 1);
        QVERIFY(display);
        QVERIFY(normalized);
        QVERIFY(verifier);

        auto created = repository.createAccount(std::move(display).value(),
                                                std::move(normalized).value(),
                                                std::move(verifier).value());
        QVERIFY(created);
        QVERIFY(created.value().createdAtUtc().isValid());
        QVERIFY(!created.value().lastLoginAtUtc().has_value());

        auto secondDisplay = devhub::domain::account::DisplayUsername::create(QStringLiteral("Second"));
        QVERIFY(secondDisplay);
        auto secondNormalized = devhub::domain::account::NormalizedUsername::fromDisplay(
            secondDisplay.value());
        QVERIFY(secondNormalized);
        auto secondVerifier = devhub::domain::account::PasswordVerifier::fromStorage(
            QStringLiteral("another-test-verifier"), 1);
        auto second = repository.createAccount(std::move(secondDisplay).value(),
                                               std::move(secondNormalized).value(),
                                               std::move(secondVerifier).value());
        QVERIFY(!second);
        QCOMPARE(second.error().code, devhub::domain::errors::ErrorCode::AccountAlreadyExists);

        auto replacement = devhub::domain::account::PasswordVerifier::fromStorage(
            QStringLiteral("replacement-test-verifier"), 1);
        QVERIFY(replacement);
        const QDateTime loggedInAt = QDateTime::currentDateTimeUtc();
        std::optional<devhub::domain::account::PasswordVerifier> replacementValue(
            std::move(replacement).value());
        QVERIFY(repository.recordSuccessfulLogin(created.value().id(), loggedInAt,
                                                 std::move(replacementValue)));
        auto username = devhub::domain::account::NormalizedUsername::create(QStringLiteral("local user"));
        QVERIFY(username);
        auto loaded = repository.findByUsername(username.value());
        QVERIFY(loaded);
        QVERIFY(loaded.value().has_value());
        QCOMPARE(loaded.value()->account.id(), created.value().id());
        QVERIFY(loaded.value()->account.lastLoginAtUtc().has_value());
        QCOMPARE(loaded.value()->account.lastLoginAtUtc()->toMSecsSinceEpoch(),
                 loggedInAt.toMSecsSinceEpoch());
    }

    void productionProfileMeasurement()
    {
        if (qEnvironmentVariableIsEmpty("DEVHUB_RUN_ARGON2_BENCHMARK")) {
            QSKIP("Set DEVHUB_RUN_ARGON2_BENCHMARK=1 to measure the interactive profile on this hardware.");
        }
        devhub::infrastructure::account::SodiumPasswordHasher hasher;
        QElapsedTimer timer;
        timer.start();
        auto verifier = hashInWorker(hasher, QStringLiteral("local Argon2id performance measurement"));
        QVERIFY(verifier);
        qInfo("Interactive Argon2id hash duration on this machine: %lld ms",
              static_cast<long long>(timer.elapsed()));
    }
};

QTEST_GUILESS_MAIN(AccountSecurityTest)
#include "account_security_test.moc"
