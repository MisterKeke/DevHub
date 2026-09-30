#pragma once

#include "../../domain/errors/AppError.h"

#include <QString>

namespace devhub::shared::security {
class RedactionService;
}

namespace devhub::presentation::errors {

struct PresentedError final
{
    QString userMessage;
    QString technicalDetails;
    bool retryable = false;
};

class ErrorPresenter final
{
public:
    explicit ErrorPresenter(const shared::security::RedactionService& redactor);
    [[nodiscard]] PresentedError present(const domain::errors::AppError& error) const;

private:
    const shared::security::RedactionService& m_redactor;
};

} // namespace devhub::presentation::errors
