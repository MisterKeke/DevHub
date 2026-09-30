#include "TechnicalDetailsFormatter.h"

#include "../../shared/security/RedactionService.h"

namespace devhub::presentation::errors {

TechnicalDetailsFormatter::TechnicalDetailsFormatter(const shared::security::RedactionService& redactor)
    : m_redactor(redactor)
{
}

QString TechnicalDetailsFormatter::format(const domain::errors::AppError& error) const
{
    QStringList details;
    details.append(QStringLiteral("Code: %1").arg(domain::errors::errorCodeName(error.code)));
    details.append(QStringLiteral("Category: %1").arg(domain::errors::errorCategoryName(error.category)));
    if (!error.operation.isEmpty()) {
        details.append(QStringLiteral("Operation: %1").arg(m_redactor.redact(error.operation)));
    }
    if (!error.technicalMessage.isEmpty()) {
        details.append(m_redactor.redact(error.technicalMessage));
    }
    if (error.nativeCode.has_value()) {
        details.append(QStringLiteral("Native code: %1").arg(*error.nativeCode));
    }
    if (error.workspaceId.has_value()) {
        details.append(QStringLiteral("Workspace: %1").arg(error.workspaceId->toString(QUuid::WithoutBraces)));
    }
    if (error.correlationId.has_value() && !error.correlationId->isNull()) {
        details.append(QStringLiteral("Correlation ID: %1").arg(error.correlationId->toString()));
    }
    return details.join(u'\n');
}

} // namespace devhub::presentation::errors
