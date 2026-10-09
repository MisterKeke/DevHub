#pragma once

#include <QUuid>
#include <QString>

#include <optional>

namespace devhub::domain::errors {

enum class ErrorCategory
{
    Authentication,
    Security,
    Persistence,
    Migration,
    Workspace,
    Git,
    Process,
    Port,
    Log,
    Database,
    FileSystem,
    Permission,
    Configuration,
    Internal
};

enum class ErrorSeverity
{
    Information,
    Warning,
    Error,
    Fatal
};

enum class ErrorCode
{
    InvalidArgument,
    StartupFailed,
    PathUnavailable,
    PathNotWritable,
    DirectoryCreationFailed,
    DatabaseUnavailable,
    DatabaseQueryFailed,
    DatabaseCorrupt,
    MigrationFailed,
    SchemaTooNew,
    InvalidUsername,
    AuthenticationFailed,
    UserDirectoryInitializationFailed,
    ProfileResetFailed,
    AccountAlreadyExists,
    AccountStoreCorrupt,
    AccountNotFound,
    InvalidPasswordHash,
    PasswordHashFailed,
    PasswordOperationBusy,
    OperationCancelled,
    ConcurrencyLimitReached,
    ShutdownInProgress,
    IoFailure,
    CodexProtocolFailure,
    CodexRequestTimeout,
    CodexConnectionLost,
    AssistantPermissionDenied,
    InternalFailure
};

struct CorrelationId
{
    QUuid value;

    [[nodiscard]] bool isNull() const noexcept { return value.isNull(); }
    [[nodiscard]] QString toString() const { return value.toString(QUuid::WithoutBraces); }

    friend bool operator==(const CorrelationId&, const CorrelationId&) = default;
};

struct AppError
{
    ErrorCode code = ErrorCode::InternalFailure;
    ErrorCategory category = ErrorCategory::Internal;
    ErrorSeverity severity = ErrorSeverity::Error;
    QString userMessage;
    QString technicalMessage;
    QString operation;
    std::optional<qint64> nativeCode;
    std::optional<QUuid> workspaceId;
    std::optional<CorrelationId> correlationId;
    bool retryable = false;

    [[nodiscard]] static AppError failure(ErrorCode errorCode,
                                          ErrorCategory errorCategory,
                                          QString userText,
                                          QString technicalText,
                                          QString operationName = {});
};

[[nodiscard]] QString errorCodeName(ErrorCode code);
[[nodiscard]] QString errorCategoryName(ErrorCategory category);

} // namespace devhub::domain::errors
