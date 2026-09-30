#pragma once

#include <QProxyStyle>

namespace devhub::presentation::theme {

class ThemeProxyStyle final : public QProxyStyle
{
public:
    explicit ThemeProxyStyle(QStyle* baseStyle);
    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget = nullptr) const override;
};

} // namespace devhub::presentation::theme
