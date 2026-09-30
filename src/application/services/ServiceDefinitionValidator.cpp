#include "ServiceDefinitionValidator.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace devhub::application::services {
shared::common::Result<domain::services::ServiceDefinition> ServiceDefinitionValidator::validate(
    domain::services::ServiceDefinition definition, const QString& workspaceRoot) const {
    auto fail = [](QString user, QString detail) {
        return shared::common::Result<domain::services::ServiceDefinition>::failure(
            domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument,
                domain::errors::ErrorCategory::Process, std::move(user), std::move(detail), QStringLiteral("validate service")));
    };
    definition.name = definition.name.trimmed(); definition.program = definition.program.trimmed();
    if (definition.name.isEmpty() || definition.name.size() > 100) return fail(QStringLiteral("Enter a service name (1–100 characters)."), QStringLiteral("Invalid service name."));
    if (definition.program.isEmpty()) return fail(QStringLiteral("Choose a program to start."), QStringLiteral("Program is empty."));
    if (definition.arguments.size() > 256) return fail(QStringLiteral("A service can have at most 256 arguments."), QStringLiteral("Argument count exceeds limit."));
    qsizetype total = definition.program.size(); for (const auto& arg : definition.arguments) total += arg.size();
    if (total > 32767) return fail(QStringLiteral("The program and arguments are too long."), QStringLiteral("Total argument size exceeds Windows process limit."));
    if (definition.workingDirectory.isEmpty()) definition.workingDirectory = workspaceRoot;
    else if (QDir::isRelativePath(definition.workingDirectory)) definition.workingDirectory = QDir(workspaceRoot).absoluteFilePath(definition.workingDirectory);
    const QFileInfo working(definition.workingDirectory);
    if (!working.exists() || !working.isDir()) return fail(QStringLiteral("The service working directory does not exist."), working.absoluteFilePath());
    static const QRegularExpression envName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    QSet<QString> seen;
    for (const auto& name : definition.environmentNames) {
        if (!envName.match(name).hasMatch() || seen.contains(name)) return fail(QStringLiteral("Environment references must be unique valid variable names."), name);
        seen.insert(name);
    }
    if (definition.stopTimeoutMs < 100 || definition.stopTimeoutMs > 120000) return fail(QStringLiteral("Stop timeout must be between 100 ms and 120 seconds."), QStringLiteral("Invalid stop timeout."));
    definition.workingDirectory = working.absoluteFilePath();
    return shared::common::Result<domain::services::ServiceDefinition>::success(std::move(definition));
}
}
