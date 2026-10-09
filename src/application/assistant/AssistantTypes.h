#pragma once

#include "domain/errors/AppError.h"

#include <QProcessEnvironment>
#include <QStringList>
#include <QMetaType>
#include <QUrl>
#include <QList>

namespace devhub::application::assistant {

enum class AssistantConnectionState { Disconnected, Probing, Starting, Ready, Stopping, Failed };
enum class AssistantTurnState { Idle, Starting, Running, Cancelling, Completed, Interrupted, Failed };

enum class CodexTransportKind { NativeProcess, InMemoryTest };
enum class CodexAccountMode { Unknown, SignedOut, ChatGpt, ApiKey, Other, Expired };
enum class CodexLoginMode { Browser, DeviceCode };
enum class CodexTurnStatus { InProgress, Completed, Failed, Interrupted, Uncertain };

struct CodexInitializeInfo final {
    QString codexHome;
    QString userAgent;
    QString platformFamily;
    QString platformOs;
};
struct CodexAccount final {
    CodexAccountMode mode = CodexAccountMode::Unknown;
    bool requiresOpenaiAuth = false;
    QString planType;
    QString identityFingerprint;
};
// Transient UI values only. Never log/export/persist a challenge.
struct CodexLoginChallenge final {
    CodexLoginMode mode = CodexLoginMode::Browser;
    QString loginId;
    QUrl url;
    QString userCode;
};
struct CodexModel final {
    QString id;
    QString model;
    QString displayName;
    bool hidden = false;
    bool isDefault = false;
    bool supportsText = true;
    QString availabilityMessage;
    // model/list is a catalog, never an entitlement check.
    bool entitlementVerified = false;
};
struct CodexModelPage final { QList<CodexModel> models; QString nextCursor; };
struct CodexItem final {
    QString threadId;
    QString turnId;
    QString id;
    QString type;
    QString text;
    bool started = false;
    bool completed = false;
};
struct CodexTurn final {
    QString threadId;
    QString id;
    CodexTurnStatus status = CodexTurnStatus::InProgress;
    QList<CodexItem> items;
    domain::errors::AppError error;
    bool hasError = false;
};
// No raw email, account ID, token, transcript or credential path is persisted.
struct CodexThreadBinding final {
    QString threadId;
    QString accountFingerprint;
    QString codexHome;
    QString scratchDirectory;
    QString profileFingerprint;
};
struct CodexThread final {
    CodexThreadBinding binding;
    QList<CodexTurn> turns;
};
struct CodexClientLimits final {
    int acknowledgementMilliseconds = 5000;
    int loginAcknowledgementMilliseconds = 15000;
    int loginLifetimeMilliseconds = 5 * 60 * 1000;
    int pendingRequests = 32;
    int serverRequests = 256;
    int earlyEvents = 64;
    qsizetype earlyEventBytes = 256 * 1024;
    qsizetype itemTextBytes = 2 * 1024 * 1024;
    qsizetype historyBytes = 8 * 1024 * 1024;
    int historyTurns = 100;
    int itemsPerTurn = 128;
};

struct CodexExecutable final {
    const QString path;
    const QString version;
};

// Immutable value snapshots. Copy construction is supported; changing a launch
// or probe result requires constructing a new value rather than editing fields.
struct CodexLaunchSpec final {
    const CodexExecutable executable;
    const QStringList arguments;
    const QString scratchDirectory;
    const QProcessEnvironment environment;
    const quint64 processGeneration = 0;
};

struct CodexTransportLimits final {
    qsizetype recordBytes = 4 * 1024 * 1024;
    qsizetype framingBytes = 8 * 1024 * 1024;
    qint64 stdinBytes = 1024 * 1024;
    qint64 writeChunkBytes = 64 * 1024;
    qsizetype stderrBytes = 64 * 1024;
    int recordsPerDispatch = 64;
    int dispatchMilliseconds = 4;
    int startMilliseconds = 5000;
    int drainMilliseconds = 2000;
    int terminateMilliseconds = 2000;
};

// Pass an explicitly filtered environment. Additional proxy/CA and CODEX_HOME
// policy belongs to the admission layer; tokens and arbitrary variables stay out.
inline QProcessEnvironment codexBaseEnvironment(const QProcessEnvironment& source)
{
    QProcessEnvironment result;
    const QStringList names = {QStringLiteral("SystemRoot"), QStringLiteral("WINDIR"),
        QStringLiteral("COMSPEC"), QStringLiteral("PATH"), QStringLiteral("PATHEXT"),
        QStringLiteral("USERPROFILE"), QStringLiteral("HOMEDRIVE"), QStringLiteral("HOMEPATH"),
        QStringLiteral("LOCALAPPDATA"), QStringLiteral("APPDATA"), QStringLiteral("TEMP"),
        QStringLiteral("TMP"), QStringLiteral("HOME"), QStringLiteral("LANG"), QStringLiteral("LC_ALL")};
    for (const auto& name : names) {
        if (source.contains(name)) result.insert(name, source.value(name));
    }
    return result;
}

} // namespace devhub::application::assistant
Q_DECLARE_METATYPE(devhub::application::assistant::CodexExecutable)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexInitializeInfo)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexAccount)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexLoginChallenge)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexModelPage)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexItem)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexTurn)
Q_DECLARE_METATYPE(devhub::application::assistant::CodexThread)
