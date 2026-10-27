#include "ThemeTokens.h"

#include <QApplication>
#include <QFontDatabase>

namespace devhub::presentation::theme {

int ThemeTokens::spacing(SpaceToken token) noexcept
{
    switch (token) {
    case SpaceToken::One: return 4;
    case SpaceToken::Two: return 8;
    case SpaceToken::Three: return 12;
    case SpaceToken::Four: return 16;
    case SpaceToken::Five: return 20;
    case SpaceToken::Six: return 24;
    }
    return 0;
}

int ThemeTokens::radius(RadiusToken token) noexcept
{
    switch (token) {
    case RadiusToken::Control:
    case RadiusToken::Card:
    case RadiusToken::Pill: return 0;
    }
    return 0;
}

int ThemeTokens::size(SizeToken token) noexcept
{
    switch (token) {
    case SizeToken::Control: return 36;
    case SizeToken::PrimaryAction: return 44;
    case SizeToken::Sidebar: return 270;
    case SizeToken::ModuleNavigation: return 165;
    case SizeToken::MinimumWindowWidth: return 1180;
    case SizeToken::MinimumWindowHeight: return 720;
    }
    return 0;
}

QString ThemeTokens::fontFamily()
{
    // Resolve once for both Widgets and Quick; fonts are supplied by the OS.
    static const QString family = [] {
        const auto available = QFontDatabase::families();
        for (const auto& candidate : {QStringLiteral("Cascadia Code"),
                                      QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")}) {
            if (available.contains(candidate, Qt::CaseInsensitive)) return candidate;
        }
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }();
    return family;
}

QFont ThemeTokens::font(TypographyToken token)
{
    QFont result = QApplication::font();
    int pointSize = 10;
    switch (token) {
    case TypographyToken::WindowTitle: pointSize = 16; break;
    case TypographyToken::PageTitle: pointSize = 15; break;
    case TypographyToken::CardTitle: pointSize = 11; break;
    case TypographyToken::Body: pointSize = 10; break;
    case TypographyToken::Metadata: pointSize = 9; break;
    case TypographyToken::Monospace:
        pointSize = 9;
        result.setStyleHint(QFont::Monospace);
        break;
    }
    result.setFamily(fontFamily());
    result.setStyleHint(QFont::Monospace);
    result.setWeight(QFont::Normal);
    result.setPointSize(pointSize);
    if (token == TypographyToken::WindowTitle || token == TypographyToken::PageTitle
        || token == TypographyToken::CardTitle) {
        result.setWeight(QFont::DemiBold);
    }
    return result;
}

} // namespace devhub::presentation::theme
