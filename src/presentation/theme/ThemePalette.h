#pragma once

#include "ThemeTokens.h"

#include <QColor>
#include <QPalette>

namespace devhub::presentation::theme {

enum class ThemeVariant { DarkRose, DarkBlue };

class ThemePalette final
{
public:
    static void setVariant(ThemeVariant variant);
    [[nodiscard]] static QColor color(ColorToken token);
    [[nodiscard]] static QPalette palette();
};

} // namespace devhub::presentation::theme
