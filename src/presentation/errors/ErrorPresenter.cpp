#include "ErrorPresenter.h"

#include "TechnicalDetailsFormatter.h"
#include "../../shared/security/RedactionService.h"

namespace devhub::presentation::errors {

ErrorPresenter::ErrorPresenter(const shared::security::RedactionService& redactor)
    : m_redactor(redactor)
{
}

PresentedError ErrorPresenter::present(const domain::errors::AppError& error) const
{
    TechnicalDetailsFormatter formatter(m_redactor);
    PresentedError presented;
    presented.userMessage = error.userMessage.isEmpty()
        ? QStringLiteral("DevHub could not complete that action.")
        : m_redactor.redact(error.userMessage);
    presented.technicalDetails = formatter.format(error);
    presented.retryable = error.retryable;
    return presented;
}

} // namespace devhub::presentation::errors
