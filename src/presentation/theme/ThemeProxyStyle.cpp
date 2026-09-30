#include "ThemeProxyStyle.h"

#include "ThemePalette.h"

#include <QPainter>
#include <QStyleOption>

namespace devhub::presentation::theme {

ThemeProxyStyle::ThemeProxyStyle(QStyle* baseStyle)
    : QProxyStyle(baseStyle)
{
}

void ThemeProxyStyle::drawPrimitive(PrimitiveElement element,
                                    const QStyleOption* option,
                                    QPainter* painter,
                                    const QWidget* widget) const
{
    if (element == PE_FrameFocusRect && option != nullptr && painter != nullptr) {
        if (!(option->state & State_KeyboardFocusChange)) {
            return;
        }
        painter->save();
        QPen pen(ThemePalette::color(ColorToken::FocusRing));
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(option->rect.adjusted(1, 1, -2, -2), 4, 4);
        painter->restore();
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

} // namespace devhub::presentation::theme
