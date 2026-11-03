#include "WindowsCredentialStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace devhub::infrastructure::github {
namespace {
QString invalidScopeMessage()
{
    return QObject::tr("DevHub could not access GitHub credentials for this local account.");
}
}

QString WindowsCredentialStore::targetName(const QString& localAccountId)
{
    static const QRegularExpression validId(QStringLiteral("^[A-Fa-f0-9-]{16,64}$"));
    if (!validId.match(localAccountId).hasMatch()) return {};
    return QStringLiteral("DevHub/GitHub/%1").arg(localAccountId.toLower());
}

bool WindowsCredentialStore::load(const QString& localAccountId,
                                  application::github::GitHubCredentials& credentials,
                                  QString& userSafeError)
{
    const QString target = targetName(localAccountId);
    if (target.isEmpty()) { userSafeError = invalidScopeMessage(); return false; }
#ifdef Q_OS_WIN
    PCREDENTIALW stored = nullptr;
    const std::wstring wideTarget = target.toStdWString();
    if (!CredReadW(wideTarget.c_str(), CRED_TYPE_GENERIC, 0, &stored)) {
        if (GetLastError() == ERROR_NOT_FOUND) return true;
        userSafeError = QObject::tr("Windows Credential Manager could not read the saved GitHub connection.");
        return false;
    }
    QByteArray blob(reinterpret_cast<const char*>(stored->CredentialBlob),
                    static_cast<qsizetype>(stored->CredentialBlobSize));
    CredFree(stored);
    const auto document = QJsonDocument::fromJson(blob);
    SecureZeroMemory(blob.data(), static_cast<SIZE_T>(blob.size()));
    if (!document.isObject()) {
        userSafeError = QObject::tr("The saved GitHub connection could not be read. Reconnect GitHub to replace it.");
        return false;
    }
    const auto object = document.object();
    const QString accessToken = object.value(QStringLiteral("accessToken")).toString();
    credentials.clientId = object.value(QStringLiteral("clientId")).toString();
    const QString login = object.value(QStringLiteral("login")).toString();
    if (accessToken.isEmpty() || login.isEmpty()) {
        userSafeError = QObject::tr("The saved GitHub connection is incomplete. Reconnect GitHub to replace it.");
        return false;
    }
    credentials.accessToken = accessToken;
    credentials.accessTokenExpiresAtUtc = QDateTime::fromString(
        object.value(QStringLiteral("accessTokenExpiresAtUtc")).toString(), Qt::ISODateWithMs).toUTC();
    credentials.refreshToken = object.value(QStringLiteral("refreshToken")).toString();
    credentials.refreshTokenExpiresAtUtc = QDateTime::fromString(
        object.value(QStringLiteral("refreshTokenExpiresAtUtc")).toString(), Qt::ISODateWithMs).toUTC();
    credentials.login = login;
    credentials.avatarUrl = object.value(QStringLiteral("avatarUrl")).toString();
    credentials.lastVerifiedAtUtc = QDateTime::fromString(
        object.value(QStringLiteral("lastVerifiedAtUtc")).toString(), Qt::ISODateWithMs).toUTC();
    return true;
#else
    Q_UNUSED(credentials);
    userSafeError = QObject::tr("Secure GitHub credential storage is available only on Windows.");
    return false;
#endif
}

bool WindowsCredentialStore::save(const QString& localAccountId,
                                  const application::github::GitHubCredentials& credentials,
                                  QString& userSafeError)
{
    const QString target = targetName(localAccountId);
    if (target.isEmpty() || credentials.accessToken.isEmpty() || credentials.login.isEmpty()) {
        userSafeError = invalidScopeMessage();
        return false;
    }
#ifdef Q_OS_WIN
    QJsonObject object;
    object.insert(QStringLiteral("accessToken"), credentials.accessToken);
    object.insert(QStringLiteral("clientId"), credentials.clientId);
    if (credentials.accessTokenExpiresAtUtc.isValid())
        object.insert(QStringLiteral("accessTokenExpiresAtUtc"), credentials.accessTokenExpiresAtUtc.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("refreshToken"), credentials.refreshToken);
    if (credentials.refreshTokenExpiresAtUtc.isValid())
        object.insert(QStringLiteral("refreshTokenExpiresAtUtc"), credentials.refreshTokenExpiresAtUtc.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("login"), credentials.login);
    object.insert(QStringLiteral("avatarUrl"), credentials.avatarUrl);
    if (credentials.lastVerifiedAtUtc.isValid())
        object.insert(QStringLiteral("lastVerifiedAtUtc"), credentials.lastVerifiedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    QByteArray blob = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        SecureZeroMemory(blob.data(), static_cast<SIZE_T>(blob.size()));
        userSafeError = QObject::tr("The saved GitHub connection is too large for Windows Credential Manager.");
        return false;
    }

    const std::wstring wideTarget = target.toStdWString();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(wideTarget.c_str());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.UserName = const_cast<LPWSTR>(L"DevHub GitHub OAuth Device Flow");
    const BOOL saved = CredWriteW(&credential, 0);
    SecureZeroMemory(blob.data(), static_cast<SIZE_T>(blob.size()));
    if (!saved) {
        userSafeError = QObject::tr("Windows Credential Manager could not securely save the GitHub connection.");
        return false;
    }
    return true;
#else
    Q_UNUSED(credentials);
    userSafeError = QObject::tr("Secure GitHub credential storage is available only on Windows.");
    return false;
#endif
}

bool WindowsCredentialStore::remove(const QString& localAccountId, QString& userSafeError)
{
    const QString target = targetName(localAccountId);
    if (target.isEmpty()) { userSafeError = invalidScopeMessage(); return false; }
#ifdef Q_OS_WIN
    const std::wstring wideTarget = target.toStdWString();
    if (CredDeleteW(wideTarget.c_str(), CRED_TYPE_GENERIC, 0)) return true;
    if (GetLastError() == ERROR_NOT_FOUND) return true;
    userSafeError = QObject::tr("Windows Credential Manager could not remove the saved GitHub connection.");
    return false;
#else
    userSafeError = QObject::tr("Secure GitHub credential storage is available only on Windows.");
    return false;
#endif
}

} // namespace devhub::infrastructure::github
