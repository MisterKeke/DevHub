#pragma once

#include "../../domain/errors/AppError.h"

#include <QString>

namespace devhub::shared::security {
class RedactionService;
}

namespace devhub::presentation::errors {

class TechnicalDetailsFormatter final
{
public:
    explicit TechnicalDetailsFormatter(const shared::security::RedactionService& redactor);
    [[nodiscard]] QString format(const domain::errors::AppError& error) const;

private:
    const shared::security::RedactionService& m_redactor;
};

} // namespace devhub::presentation::errors
