#pragma once
#include <QStackedWidget>

namespace devhub::presentation::widgets {
// Hidden module pages must not enlarge the active module's window.
class CurrentPageStack final : public QStackedWidget {
public:
    explicit CurrentPageStack(QWidget* parent = nullptr) : QStackedWidget(parent) {
        connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
    }
    QSize sizeHint() const override {
        return currentWidget() ? currentWidget()->sizeHint() : QSize{};
    }
    QSize minimumSizeHint() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()) : QSize{};
    }
};
}
