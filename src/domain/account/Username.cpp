#include "Username.h"

#include <QChar>
#include <QList>

#include <utility>

namespace devhub::domain::account {
namespace {

shared::common::Result<DisplayUsername> invalidDisplayUsername()
{
    return shared::common::Result<DisplayUsername>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidUsername,
        domain::errors::ErrorCategory::Authentication,
        QStringLiteral("Choose a username with 1 to 64 characters. Avoid control characters and path separators."),
        QStringLiteral("Username failed the documented length or character validation."),
        QStringLiteral("validate username")));
}

shared::common::Result<NormalizedUsername> invalidNormalizedUsername()
{
    return shared::common::Result<NormalizedUsername>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidUsername,
        domain::errors::ErrorCategory::Authentication,
        QStringLiteral("Choose a username with 1 to 64 characters. Avoid control characters and path separators."),
        QStringLiteral("Normalized username failed the documented length or character validation."),
        QStringLiteral("normalize username")));
}

bool validCharacters(const QString& value)
{
    for (const QChar character : value) {
        if (character.category() == QChar::Other_Control
            || character == QLatin1Char('/') || character == QLatin1Char('\\')) {
            return false;
        }
    }
    return true;
}

bool validLength(const QString& value)
{
    const qsizetype length = value.toUcs4().size();
    return length >= 1 && length <= MaximumUsernameCodePoints;
}

} // namespace

DisplayUsername::DisplayUsername(QString value)
    : m_value(std::move(value))
{
}

shared::common::Result<DisplayUsername> DisplayUsername::create(QString value)
{
    value = value.trimmed();
    if (!validLength(value) || !validCharacters(value)) {
        return invalidDisplayUsername();
    }
    return shared::common::Result<DisplayUsername>::success(DisplayUsername(std::move(value)));
}

NormalizedUsername::NormalizedUsername(QString value)
    : m_value(std::move(value))
{
}

shared::common::Result<NormalizedUsername> NormalizedUsername::fromDisplay(const DisplayUsername& value)
{
    return create(value.value());
}

shared::common::Result<NormalizedUsername> NormalizedUsername::create(QString value)
{
    // Qt's simple case fold keeps sharp s as one character; usernames treat it
    // like "ss" so that Straße and STRASSE resolve to the same local account.
    value = value.trimmed()
                .normalized(QString::NormalizationForm_KC)
                .toCaseFolded()
                .replace(QChar(u'\u00DF'), QStringLiteral("ss"))
                .normalized(QString::NormalizationForm_KC);
    if (!validLength(value) || !validCharacters(value)) {
        return invalidNormalizedUsername();
    }
    return shared::common::Result<NormalizedUsername>::success(NormalizedUsername(std::move(value)));
}

} // namespace devhub::domain::account
