#pragma once

#include <QFont>
#include <QString>

namespace devhub::presentation::theme {

enum class ColorToken
{
    WindowBackground,
    AppSurface,
    SidebarSurface,
    PanelSurface,
    RaisedSurface,
    HoverSurface,
    ActiveSurface,
    BorderSubtle,
    BorderStrong,
    TextPrimary,
    TextSecondary,
    TextMuted,
    Accent,
    AccentHover,
    AccentPressed,
    AccentSoft,
    Success,
    Warning,
    Error,
    Info,
    FocusRing
};

enum class SpaceToken { One, Two, Three, Four, Five, Six };
enum class RadiusToken { Control, Card, Pill };
enum class SizeToken { Control, PrimaryAction, Sidebar, ModuleNavigation, MinimumWindowWidth, MinimumWindowHeight };
enum class TypographyToken { WindowTitle, PageTitle, CardTitle, Body, Metadata, Monospace };

class ThemeTokens final
{
public:
    [[nodiscard]] static int spacing(SpaceToken token) noexcept;
    [[nodiscard]] static int radius(RadiusToken token) noexcept;
    [[nodiscard]] static int size(SizeToken token) noexcept;
    [[nodiscard]] static QString fontFamily();
    [[nodiscard]] static QFont font(TypographyToken token);
};

} // namespace devhub::presentation::theme
