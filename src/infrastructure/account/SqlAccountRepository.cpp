#include "SqlAccountRepository.h"

#include "../database/SqlErrorMapper.h"
#include "../database/SqlTransactionGuard.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

#include <utility>

namespace devhub::infrastructure::account {
namespace {

using domain::errors::AppError;
using domain::errors::ErrorCategory;
using domain::errors::ErrorCode;

template<typename T>
shared::common::Result<T> accountError(ErrorCode code,
                                       const QString& message,
                                       const QString& details,
                                       const QString& operation)
{
    return shared::common::Result<T>::failure(AppError::failure(
        code, ErrorCategory::Persistence, message, details, operation));
}

QString timestampText(const QDateTime& timestamp)
{
    return timestamp.toUTC().toString(Qt::ISODateWithMs);
}

const QString accountColumns = QStringLiteral(
    "id, display_username, username_normalized, password_hash, password_scheme_version, "
    "created_at_utc, updated_at_utc, last_login_at_utc");

} // namespace

SqlAccountRepository::SqlAccountRepository(database::InternalDatabaseManager& databaseManager,
                                           shared::identifiers::IIdGenerator& idGenerator)
    : m_databaseManager(databaseManager), m_idGenerator(idGenerator)
{
}

shared::common::Result<void> SqlAccountRepository::ensureOwnerThread(const QString& operation) const
{
    if (QThread::currentThread() == m_databaseManager.thread()
        && m_databaseManager.database().isOpen()) {
        return shared::common::Result<void>::success();
    }
    return shared::common::Result<void>::failure(AppError::failure(
        ErrorCode::DatabaseUnavailable,
        ErrorCategory::Database,
        QStringLiteral("DevHub could not access the local account store."),
        QStringLiteral("The account repository requires an open database in its owning thread."),
        operation));
}

shared::common::Result<application::accounts::AccountCredentials>
SqlAccountRepository::readCredentials(QSqlQuery& query) const
{
    auto idResult = domain::account::AccountId::fromString(query.value(0).toString());
    auto displayResult = domain::account::DisplayUsername::create(query.value(1).toString());
    auto normalizedResult = domain::account::NormalizedUsername::create(query.value(2).toString());
    auto verifierResult = domain::account::PasswordVerifier::fromStorage(
        query.value(3).toString(), query.value(4).toInt());
    const QDateTime createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs).toUTC();
    const QDateTime updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs).toUTC();
    const QString lastLoginText = query.value(7).toString();
    const std::optional<QDateTime> lastLogin = lastLoginText.isEmpty()
        ? std::nullopt
        : std::optional<QDateTime>(QDateTime::fromString(lastLoginText, Qt::ISODateWithMs).toUTC());

    if (!idResult || !displayResult || !normalizedResult || !verifierResult
        || !createdAt.isValid() || !updatedAt.isValid()
        || (lastLogin.has_value() && !lastLogin->isValid())) {
        return accountError<application::accounts::AccountCredentials>(
            ErrorCode::AccountStoreCorrupt,
            QStringLiteral("The local account data is invalid."),
            QStringLiteral("Stored account row failed identifier, username, verifier, or timestamp validation."),
            QStringLiteral("read local account"));
    }
    auto computedNormalized = domain::account::NormalizedUsername::fromDisplay(displayResult.value());
    if (!computedNormalized || computedNormalized.value() != normalizedResult.value()) {
        return accountError<application::accounts::AccountCredentials>(
            ErrorCode::AccountStoreCorrupt,
            QStringLiteral("The local account data is invalid."),
            QStringLiteral("Stored normalized username does not match its display username."),
            QStringLiteral("read local account"));
    }

    domain::account::Account account(idResult.value(),
                                     std::move(displayResult).value(),
                                     std::move(normalizedResult).value(),
                                     createdAt,
                                     updatedAt,
                                     lastLogin);
    application::accounts::AccountCredentials credentials(std::move(account),
                                                           std::move(verifierResult).value());
    return shared::common::Result<application::accounts::AccountCredentials>::success(
        std::move(credentials));
}

shared::common::Result<std::optional<application::accounts::AccountCredentials>>
SqlAccountRepository::findByUsername(const domain::account::NormalizedUsername& username)
{
    const QString operation = QStringLiteral("find account by username");
    auto ownerResult = ensureOwnerThread(operation);
    if (!ownerResult) {
        return shared::common::Result<std::optional<application::accounts::AccountCredentials>>::failure(
            ownerResult.error());
    }
    QSqlQuery query(m_databaseManager.database());
    query.prepare(QStringLiteral("SELECT %1 FROM accounts WHERE username_normalized = ?")
                      .arg(accountColumns));
    query.addBindValue(username.value());
    if (!query.exec()) {
        return shared::common::Result<std::optional<application::accounts::AccountCredentials>>::failure(
            database::SqlErrorMapper::fromSqlError(query.lastError(), operation));
    }
    if (!query.next()) {
        return shared::common::Result<std::optional<application::accounts::AccountCredentials>>::success(
            std::nullopt);
    }
    auto credentials = readCredentials(query);
    if (!credentials) {
        return shared::common::Result<std::optional<application::accounts::AccountCredentials>>::failure(
            credentials.error());
    }
    if (query.next()) {
        return accountError<std::optional<application::accounts::AccountCredentials>>(
            ErrorCode::AccountStoreCorrupt,
            QStringLiteral("The local account data is invalid."),
            QStringLiteral("Normalized username query returned duplicate account rows."),
            operation);
    }
    std::optional<application::accounts::AccountCredentials> value(std::move(credentials).value());
    return shared::common::Result<std::optional<application::accounts::AccountCredentials>>::success(
        std::move(value));
}

shared::common::Result<std::optional<domain::account::Account>>
SqlAccountRepository::findSingleAccount()
{
    const QString operation = QStringLiteral("find single local account");
    auto ownerResult = ensureOwnerThread(operation);
    if (!ownerResult) {
        return shared::common::Result<std::optional<domain::account::Account>>::failure(ownerResult.error());
    }
    QSqlQuery query(m_databaseManager.database());
    if (!query.exec(QStringLiteral("SELECT %1 FROM accounts LIMIT 2").arg(accountColumns))) {
        return shared::common::Result<std::optional<domain::account::Account>>::failure(
            database::SqlErrorMapper::fromSqlError(query.lastError(), operation));
    }
    if (!query.next()) {
        return shared::common::Result<std::optional<domain::account::Account>>::success(std::nullopt);
    }
    auto credentials = readCredentials(query);
    if (!credentials) {
        return shared::common::Result<std::optional<domain::account::Account>>::failure(credentials.error());
    }
    if (query.next()) {
        return accountError<std::optional<domain::account::Account>>(
            ErrorCode::AccountStoreCorrupt,
            QStringLiteral("The local account data is invalid."),
            QStringLiteral("More than one local account violates the v1 single-account rule."),
            operation);
    }
    std::optional<domain::account::Account> value(std::move(credentials).value().account);
    return shared::common::Result<std::optional<domain::account::Account>>::success(std::move(value));
}

shared::common::Result<domain::account::Account>
SqlAccountRepository::createAccount(domain::account::DisplayUsername displayUsername,
                                    domain::account::NormalizedUsername normalizedUsername,
                                    domain::account::PasswordVerifier verifier)
{
    auto idResult = domain::account::AccountId::create(m_idGenerator);
    if (!idResult) {
        return shared::common::Result<domain::account::Account>::failure(idResult.error());
    }
    return createAccountWithId(std::move(idResult).value(), std::move(displayUsername),
                               std::move(normalizedUsername), std::move(verifier));
}

shared::common::Result<domain::account::Account> SqlAccountRepository::createAccountWithId(
    domain::account::AccountId id,
    domain::account::DisplayUsername displayUsername,
    domain::account::NormalizedUsername normalizedUsername,
    domain::account::PasswordVerifier verifier)
{
    const QString operation = QStringLiteral("create local account");
    auto ownerResult = ensureOwnerThread(operation);
    if (!ownerResult) {
        return shared::common::Result<domain::account::Account>::failure(ownerResult.error());
    }
    auto computedNormalized = domain::account::NormalizedUsername::fromDisplay(displayUsername);
    if (!computedNormalized || computedNormalized.value() != normalizedUsername) {
        return accountError<domain::account::Account>(
            ErrorCode::InvalidUsername,
            QStringLiteral("Choose a valid username."),
            QStringLiteral("Display and normalized username values do not match."),
            operation);
    }
    database::SqlTransactionGuard transaction(m_databaseManager.database());
    if (!transaction.active()) {
        return shared::common::Result<domain::account::Account>::failure(*transaction.error());
    }
    QSqlQuery existing(m_databaseManager.database());
    if (!existing.exec(QStringLiteral("SELECT 1 FROM accounts LIMIT 1"))) {
        return shared::common::Result<domain::account::Account>::failure(
            database::SqlErrorMapper::fromSqlError(existing.lastError(), operation));
    }
    if (existing.next()) {
        return accountError<domain::account::Account>(
            ErrorCode::AccountAlreadyExists,
            QStringLiteral("A DevHub account already exists on this device."),
            QStringLiteral("The v1 account repository accepts exactly one account."),
            operation);
    }
    existing.finish();

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QSqlQuery insert(m_databaseManager.database());
    insert.prepare(QStringLiteral(
        "INSERT INTO accounts(id, username_normalized, display_username, password_hash, "
        "password_scheme_version, created_at_utc, updated_at_utc, last_login_at_utc) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, NULL)"));
    insert.addBindValue(id.toString());
    insert.addBindValue(normalizedUsername.value());
    insert.addBindValue(displayUsername.value());
    insert.addBindValue(verifier.m_encoded);
    insert.addBindValue(verifier.schemeVersion());
    insert.addBindValue(timestampText(now));
    insert.addBindValue(timestampText(now));
    if (!insert.exec()) {
        if (insert.lastError().nativeErrorCode() == QStringLiteral("19")) {
            return accountError<domain::account::Account>(
                ErrorCode::AccountAlreadyExists,
                QStringLiteral("A DevHub account already exists on this device."),
                QStringLiteral("SQLite rejected the row because of a uniqueness constraint."),
                operation);
        }
        return shared::common::Result<domain::account::Account>::failure(
            database::SqlErrorMapper::fromSqlError(insert.lastError(), operation));
    }
    auto commitResult = transaction.commit();
    if (!commitResult) {
        return shared::common::Result<domain::account::Account>::failure(commitResult.error());
    }
    return shared::common::Result<domain::account::Account>::success(
        domain::account::Account(std::move(id),
                                 std::move(displayUsername),
                                 std::move(normalizedUsername),
                                 now,
                                 now,
                                 std::nullopt));
}

shared::common::Result<void> SqlAccountRepository::deleteAccount(const domain::account::AccountId& id)
{
    const QString operation = QStringLiteral("roll back local account creation");
    auto ownerResult = ensureOwnerThread(operation);
    if (!ownerResult) {
        return ownerResult;
    }
    database::SqlTransactionGuard transaction(m_databaseManager.database());
    if (!transaction.active()) {
        return shared::common::Result<void>::failure(*transaction.error());
    }
    QSqlQuery remove(m_databaseManager.database());
    remove.prepare(QStringLiteral("DELETE FROM accounts WHERE id = ?"));
    remove.addBindValue(id.toString());
    if (!remove.exec()) {
        return shared::common::Result<void>::failure(
            database::SqlErrorMapper::fromSqlError(remove.lastError(), operation));
    }
    if (remove.numRowsAffected() != 1) {
        return accountError<void>(ErrorCode::AccountNotFound,
                                  QStringLiteral("The incomplete local account could not be rolled back."),
                                  QStringLiteral("No account row matched the generated identifier."),
                                  operation);
    }
    return transaction.commit();
}

shared::common::Result<void> SqlAccountRepository::replacePasswordVerifier(
    const domain::account::AccountId& id,
    domain::account::PasswordVerifier verifier,
    const QDateTime& updatedAtUtc)
{
    const QString operation = QStringLiteral("change local account password");
    auto ownerResult = ensureOwnerThread(operation);
    if (!ownerResult) {
        return ownerResult;
    }
    if (!updatedAtUtc.isValid()) {
        return accountError<void>(ErrorCode::InvalidArgument,
                                  QStringLiteral("DevHub could not update the account security settings."),
                                  QStringLiteral("The password update timestamp is invalid."),
                                  operation);
    }
    database::SqlTransactionGuard transaction(m_databaseManager.database());
    if (!transaction.active()) {
        return shared::common::Result<void>::failure(*transaction.error());
    }
    QSqlQuery update(m_databaseManager.database());
    update.prepare(QStringLiteral(
        "UPDATE accounts SET password_hash = ?, password_scheme_version = ?, updated_at_utc = ? WHERE id = ?"));
    update.addBindValue(verifier.m_encoded);
    update.addBindValue(verifier.schemeVersion());
    update.addBindValue(timestampText(updatedAtUtc));
    update.addBindValue(id.toString());
    if (!update.exec()) {
        return shared::common::Result<void>::failure(
            database::SqlErrorMapper::fromSqlError(update.lastError(), operation));
    }
    if (update.numRowsAffected() != 1) {
        return accountError<void>(ErrorCode::AccountNotFound,
                                  QStringLiteral("The local account could not be updated."),
                                  QStringLiteral("No account row matched the supplied identifier."),
                                  operation);
    }
    return transaction.commit();
}

shared::common::Result<void>
SqlAccountRepository::recordSuccessfulLogin(
    const domain::account::AccountId& id,
    const QDateTime& timestampUtc,
    std::optional<domain::account::PasswordVerifier> replacementVerifier)
{
    const QString operation = QStringLiteral("record successful account login");
    auto ownerResult = ensureOwnerThread(operation);
    if (!ownerResult) {
        return ownerResult;
    }
    if (!timestampUtc.isValid()) {
        return shared::common::Result<void>::failure(AppError::failure(
            ErrorCode::InvalidArgument,
            ErrorCategory::Persistence,
            QStringLiteral("DevHub could not update the account sign-in time."),
            QStringLiteral("The login timestamp is invalid."),
            operation));
    }
    database::SqlTransactionGuard transaction(m_databaseManager.database());
    if (!transaction.active()) {
        return shared::common::Result<void>::failure(*transaction.error());
    }
    QSqlQuery update(m_databaseManager.database());
    if (replacementVerifier.has_value()) {
        update.prepare(QStringLiteral(
            "UPDATE accounts SET last_login_at_utc = ?, password_hash = ?, "
            "password_scheme_version = ?, updated_at_utc = ? WHERE id = ?"));
        update.addBindValue(timestampText(timestampUtc));
        update.addBindValue(replacementVerifier->m_encoded);
        update.addBindValue(replacementVerifier->schemeVersion());
        update.addBindValue(timestampText(timestampUtc));
        update.addBindValue(id.toString());
    } else {
        update.prepare(QStringLiteral("UPDATE accounts SET last_login_at_utc = ? WHERE id = ?"));
        update.addBindValue(timestampText(timestampUtc));
        update.addBindValue(id.toString());
    }
    if (!update.exec()) {
        return shared::common::Result<void>::failure(
            database::SqlErrorMapper::fromSqlError(update.lastError(), operation));
    }
    if (update.numRowsAffected() != 1) {
        return accountError<void>(ErrorCode::AccountNotFound,
                                  QStringLiteral("The local account could not be updated."),
                                  QStringLiteral("No account row matched the supplied identifier."),
                                  operation);
    }
    return transaction.commit();
}

} // namespace devhub::infrastructure::account
