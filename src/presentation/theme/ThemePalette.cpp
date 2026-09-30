#include "ThemePalette.h"

namespace devhub::presentation::theme {
namespace {
ThemeVariant activeVariant = ThemeVariant::DarkRose;
}

void ThemePalette::setVariant(ThemeVariant variant)
{
    activeVariant = variant;
}

QColor ThemePalette::color(ColorToken token)
{
    if (activeVariant == ThemeVariant::DarkBlue) {
        switch (token) {
        case ColorToken::WindowBackground: return QColor(QStringLiteral("#08111F"));
        case ColorToken::AppSurface: return QColor(QStringLiteral("#0B1729"));
        case ColorToken::SidebarSurface: return QColor(QStringLiteral("#0D1C32"));
        case ColorToken::PanelSurface: return QColor(QStringLiteral("#10233D"));
        case ColorToken::RaisedSurface: return QColor(QStringLiteral("#17304E"));
        case ColorToken::HoverSurface: return QColor(QStringLiteral("#1C3A5C"));
        case ColorToken::ActiveSurface: return QColor(QStringLiteral("#18436B"));
        case ColorToken::BorderSubtle: return QColor(QStringLiteral("#2B4562"));
        case ColorToken::BorderStrong: return QColor(QStringLiteral("#3B638C"));
        case ColorToken::TextPrimary: return QColor(QStringLiteral("#EFF6FF"));
        case ColorToken::TextSecondary: return QColor(QStringLiteral("#B4C8DF"));
        case ColorToken::TextMuted: return QColor(QStringLiteral("#829AB3"));
        case ColorToken::Accent: return QColor(QStringLiteral("#4A9FFF"));
        case ColorToken::AccentHover: return QColor(QStringLiteral("#71B4FF"));
        case ColorToken::AccentPressed: return QColor(QStringLiteral("#2E83E6"));
        case ColorToken::AccentSoft: return QColor(74, 159, 255, 38);
        case ColorToken::Success: return QColor(QStringLiteral("#35C98A"));
        case ColorToken::Warning: return QColor(QStringLiteral("#F5BC59"));
        case ColorToken::Error: return QColor(QStringLiteral("#FF6875"));
        case ColorToken::Info: return QColor(QStringLiteral("#64B6FF"));
        case ColorToken::FocusRing: return QColor(QStringLiteral("#8BC5FF"));
        }
    }
    switch (token) {
    case ColorToken::WindowBackground: return QColor(QStringLiteral("#09080B"));
    case ColorToken::AppSurface: return QColor(QStringLiteral("#0F0D12"));
    case ColorToken::SidebarSurface: return QColor(QStringLiteral("#111014"));
    case ColorToken::PanelSurface: return QColor(QStringLiteral("#17131A"));
    case ColorToken::RaisedSurface: return QColor(QStringLiteral("#1D1720"));
    case ColorToken::HoverSurface: return QColor(QStringLiteral("#251A25"));
    case ColorToken::ActiveSurface: return QColor(QStringLiteral("#351527"));
    case ColorToken::BorderSubtle: return QColor(QStringLiteral("#2C2430"));
    case ColorToken::BorderStrong: return QColor(QStringLiteral("#513047"));
    case ColorToken::TextPrimary: return QColor(QStringLiteral("#F5F2F5"));
    case ColorToken::TextSecondary: return QColor(QStringLiteral("#A9A1AB"));
    case ColorToken::TextMuted: return QColor(QStringLiteral("#756E78"));
    case ColorToken::Accent: return QColor(QStringLiteral("#FF2D7A"));
    case ColorToken::AccentHover: return QColor(QStringLiteral("#FF478A"));
    case ColorToken::AccentPressed: return QColor(QStringLiteral("#DB1F64"));
    case ColorToken::AccentSoft: return QColor(255, 45, 122, 28);
    case ColorToken::Success: return QColor(QStringLiteral("#22C978"));
    case ColorToken::Warning: return QColor(QStringLiteral("#F4B740"));
    case ColorToken::Error: return QColor(QStringLiteral("#FF525D"));
    case ColorToken::Info: return QColor(QStringLiteral("#50A7FF"));
    case ColorToken::FocusRing: return QColor(QStringLiteral("#FF69A1"));
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
    result.setColor(QPalette::HighlightedText, color(ColorToken::TextPrimary));
    result.setColor(QPalette::ToolTipBase, color(ColorToken::RaisedSurface));
    result.setColor(QPalette::ToolTipText, color(ColorToken::TextPrimary));
    result.setColor(QPalette::Link, color(ColorToken::Accent));
    result.setColor(QPalette::LinkVisited, color(ColorToken::AccentHover));
    return result;
}

} // namespace devhub::presentation::theme
