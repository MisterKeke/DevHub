#include "ThemePalette.h"

namespace devhub::presentation::theme {
namespace {
ThemeVariant activeVariant = ThemeVariant::DarkRose;
}

void ThemePalette::setVariant(ThemeVariant variant)
{
    activeVariant = variant;
}

ThemeVariant ThemePalette::variant()
{
    return activeVariant;
}

QColor ThemePalette::color(ColorToken token)
{
    // Both existing appearance choices share the retro navy surfaces and semantics.
    // Dark Blue retains its blue primary accent; Dark Rose uses arcade pink.
    const bool blue = activeVariant == ThemeVariant::DarkBlue;
    switch (token) {
    case ColorToken::WindowBackground: return QColor(QStringLiteral("#080F24"));
    case ColorToken::AppSurface: return QColor(QStringLiteral("#080F24"));
    case ColorToken::SidebarSurface: return QColor(QStringLiteral("#0C1730"));
    case ColorToken::PanelSurface: return QColor(QStringLiteral("#101F40"));
    case ColorToken::RaisedSurface: return QColor(QStringLiteral("#152A52"));
    case ColorToken::HoverSurface: return QColor(QStringLiteral("#1D3864"));
    case ColorToken::ActiveSurface: return QColor(blue ? QStringLiteral("#18385C") : QStringLiteral("#38223F"));
    case ColorToken::BorderSubtle: return QColor(QStringLiteral("#365B91"));
    case ColorToken::BorderStrong: return QColor(QStringLiteral("#528EFF"));
    case ColorToken::TextPrimary: return QColor(QStringLiteral("#F3F5FF"));
    case ColorToken::TextSecondary: return QColor(QStringLiteral("#9AB6DD"));
    case ColorToken::TextMuted: return QColor(QStringLiteral("#7892BA"));
    case ColorToken::Accent: return QColor(blue ? QStringLiteral("#528EFF") : QStringLiteral("#FF5FA8"));
    case ColorToken::AccentHover: return QColor(blue ? QStringLiteral("#80AEFF") : QStringLiteral("#FF85BD"));
    case ColorToken::AccentPressed: return QColor(blue ? QStringLiteral("#4078E0") : QStringLiteral("#DE468E"));
    case ColorToken::AccentSoft: return blue ? QColor(82, 142, 255, 40) : QColor(255, 95, 168, 36);
    case ColorToken::Success: return QColor(QStringLiteral("#43E6A0"));
    case ColorToken::Warning: return QColor(QStringLiteral("#FFBF54"));
    case ColorToken::Error: return QColor(QStringLiteral("#FF6680"));
    case ColorToken::Info: return QColor(QStringLiteral("#56DFFF"));
    case ColorToken::FocusRing: return QColor(QStringLiteral("#56DFFF"));
    }
    return QColor();
}

QPalette ThemePalette::palette()
{
    QPalette result;
    result.setColor(QPalette::Window, color(ColorToken::WindowBackground));
    result.setColor(QPalette::Base, color(ColorToken::AppSurface));
    result.setColor(QPalette::AlternateBase, color(ColorToken::PanelSurface));
    result.setColor(QPalette::Button, color(ColorToken::PanelSurface));
    result.setColor(QPalette::Light, color(ColorToken::RaisedSurface));
    result.setColor(QPalette::Mid, color(ColorToken::BorderSubtle));
    result.setColor(QPalette::Dark, color(ColorToken::WindowBackground));
    result.setColor(QPalette::Shadow, color(ColorToken::WindowBackground));
    result.setColor(QPalette::WindowText, color(ColorToken::TextPrimary));
    result.setColor(QPalette::Text, color(ColorToken::TextPrimary));
    result.setColor(QPalette::ButtonText, color(ColorToken::TextPrimary));
    result.setColor(QPalette::PlaceholderText, color(ColorToken::TextMuted));
    result.setColor(QPalette::Highlight, color(ColorToken::Accent));
    result.setColor(QPalette::HighlightedText, color(ColorToken::WindowBackground));
    result.setColor(QPalette::ToolTipBase, color(ColorToken::RaisedSurface));
    result.setColor(QPalette::ToolTipText, color(ColorToken::TextPrimary));
    result.setColor(QPalette::Link, color(ColorToken::Accent));
    result.setColor(QPalette::LinkVisited, color(ColorToken::AccentHover));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        result.setColor(QPalette::Disabled, role, color(ColorToken::TextMuted));
    }
    result.setColor(QPalette::Disabled, QPalette::Highlight, color(ColorToken::RaisedSurface));
    result.setColor(QPalette::Disabled, QPalette::HighlightedText, color(ColorToken::TextMuted));
    return result;
}

} // namespace devhub::presentation::theme
