#include "ThemeTokens.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontInfo>

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
    case RadiusToken::Control: return 6;
    case RadiusToken::Card: return 8;
    case RadiusToken::Pill: return 999;
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

QFont ThemeTokens::font(TypographyToken token)
{
    QFont result = QApplication::font();
    QString family = QStringLiteral("Segoe UI");
    int pointSize = 10;
    switch (token) {
    case TypographyToken::WindowTitle: pointSize = 16; break;
    case TypographyToken::PageTitle: pointSize = 15; break;
    case TypographyToken::CardTitle: pointSize = 11; break;
    case TypographyToken::Body: pointSize = 10; break;
    case TypographyToken::Metadata: pointSize = 9; break;
    case TypographyToken::Monospace:
        family = QFontDatabase::families().contains(QStringLiteral("Cascadia Mono"))
            ? QStringLiteral("Cascadia Mono") : QStringLiteral("Consolas");
        pointSize = 9;
        result.setStyleHint(QFont::Monospace);
        break;
    }
    result.setFamily(family);
    result.setPointSize(pointSize);
    if (token == TypographyToken::WindowTitle || token == TypographyToken::PageTitle
        || token == TypographyToken::CardTitle) {
        result.setWeight(QFont::DemiBold);
    }
    return result;
}

} // namespace devhub::presentation::theme
