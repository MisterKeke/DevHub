#include "AppError.h"

#include <utility>

namespace devhub::domain::errors {

AppError AppError::failure(ErrorCode errorCode,
                           ErrorCategory errorCategory,
                           QString userText,
                           QString technicalText,
                           QString operationName)
{
    AppError error;
    error.code = errorCode;
    error.category = errorCategory;
    error.userMessage = std::move(userText);
    error.technicalMessage = std::move(technicalText);
    error.operation = std::move(operationName);
    return error;
}

QString errorCodeName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::InvalidArgument: return QStringLiteral("invalid_argument");
    case ErrorCode::StartupFailed: return QStringLiteral("startup_failed");
    case ErrorCode::PathUnavailable: return QStringLiteral("path_unavailable");
    case ErrorCode::PathNotWritable: return QStringLiteral("path_not_writable");
    case ErrorCode::DirectoryCreationFailed: return QStringLiteral("directory_creation_failed");
    case ErrorCode::DatabaseUnavailable: return QStringLiteral("database_unavailable");
    case ErrorCode::DatabaseQueryFailed: return QStringLiteral("database_query_failed");
    case ErrorCode::DatabaseCorrupt: return QStringLiteral("database_corrupt");
    case ErrorCode::MigrationFailed: return QStringLiteral("migration_failed");
    case ErrorCode::SchemaTooNew: return QStringLiteral("schema_too_new");
    case ErrorCode::InvalidUsername: return QStringLiteral("invalid_username");
    case ErrorCode::AuthenticationFailed: return QStringLiteral("authentication_failed");
    case ErrorCode::UserDirectoryInitializationFailed: return QStringLiteral("user_directory_initialization_failed");
    case ErrorCode::ProfileResetFailed: return QStringLiteral("profile_reset_failed");
    case ErrorCode::AccountAlreadyExists: return QStringLiteral("account_already_exists");
    case ErrorCode::AccountStoreCorrupt: return QStringLiteral("account_store_corrupt");
    case ErrorCode::AccountNotFound: return QStringLiteral("account_not_found");
    case ErrorCode::InvalidPasswordHash: return QStringLiteral("invalid_password_hash");
    case ErrorCode::PasswordHashFailed: return QStringLiteral("password_hash_failed");
    case ErrorCode::PasswordOperationBusy: return QStringLiteral("password_operation_busy");
    case ErrorCode::OperationCancelled: return QStringLiteral("operation_cancelled");
    case ErrorCode::ConcurrencyLimitReached: return QStringLiteral("concurrency_limit_reached");
    case ErrorCode::ShutdownInProgress: return QStringLiteral("shutdown_in_progress");
    case ErrorCode::IoFailure: return QStringLiteral("io_failure");
    case ErrorCode::CodexProtocolFailure: return QStringLiteral("codex_protocol_failure");
    case ErrorCode::CodexRequestTimeout: return QStringLiteral("codex_request_timeout");
    case ErrorCode::CodexConnectionLost: return QStringLiteral("codex_connection_lost");
    case ErrorCode::AssistantPermissionDenied: return QStringLiteral("assistant_permission_denied");
    case ErrorCode::AssistantLimitExceeded: return QStringLiteral("assistant_limit_exceeded");
    case ErrorCode::AssistantHistoryUnavailable: return QStringLiteral("assistant_history_unavailable");
    case ErrorCode::InternalFailure: return QStringLiteral("internal_failure");
    }
    return QStringLiteral("unknown_error");
}

QString errorCategoryName(ErrorCategory category)
{
    switch (category) {
    case ErrorCategory::Authentication: return QStringLiteral("authentication");
    case ErrorCategory::Security: return QStringLiteral("security");
    case ErrorCategory::Persistence: return QStringLiteral("persistence");
    case ErrorCategory::Migration: return QStringLiteral("migration");
    case ErrorCategory::Workspace: return QStringLiteral("workspace");
    case ErrorCategory::Git: return QStringLiteral("git");
    case ErrorCategory::Process: return QStringLiteral("process");
    case ErrorCategory::Port: return QStringLiteral("port");
    case ErrorCategory::Log: return QStringLiteral("log");
    case ErrorCategory::Database: return QStringLiteral("database");
    case ErrorCategory::FileSystem: return QStringLiteral("filesystem");
    case ErrorCategory::Permission: return QStringLiteral("permission");
    case ErrorCategory::Configuration: return QStringLiteral("configuration");
    case ErrorCategory::Internal: return QStringLiteral("internal");
    }
    return QStringLiteral("unknown");
}

} // namespace devhub::domain::errors
