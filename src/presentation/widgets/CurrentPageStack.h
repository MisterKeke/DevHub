#pragma once
#include <QStackedLayout>
#include <QWidget>

namespace devhub::presentation::widgets {
// Hidden module pages must not enlarge the active module's window.
class CurrentPageStack final : public QWidget {
    // QWidgetItem queries a widget's installed layout directly for wrapped
    // heights. Widget virtuals alone cannot override QStackedLayout's use of
    // every hidden page, so constrain the actual layout as well.
    class PageLayout final : public QStackedLayout {
    public:
        explicit PageLayout(QWidget* parent) : QStackedLayout(parent) { setContentsMargins(0, 0, 0, 0); }
        QSize sizeHint() const override {
            return currentWidget() ? currentWidget()->sizeHint() : QSize{};
        }
        QSize minimumSize() const override {
            return currentWidget() ? currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()) : QSize{};
        }
        bool hasHeightForWidth() const override {
            return currentWidget() && currentWidget()->hasHeightForWidth();
        }
        int heightForWidth(int width) const override {
            return currentWidget() ? currentWidget()->heightForWidth(width) : -1;
        }
    };
public:
    explicit CurrentPageStack(QWidget* parent = nullptr) : QWidget(parent), m_pages(new PageLayout(this)) {
        setAttribute(Qt::WA_StyledBackground, true);
        connect(m_pages, &QStackedLayout::currentChanged, this, [this] { updateGeometry(); });
    }
    int addWidget(QWidget* widget) { return m_pages->addWidget(widget); }
    void setCurrentWidget(QWidget* widget) { m_pages->setCurrentWidget(widget); }
    QWidget* currentWidget() const { return m_pages->currentWidget(); }
    int currentIndex() const { return m_pages->currentIndex(); }
    QSize sizeHint() const override {
        return m_pages->sizeHint();
    }
    QSize minimumSizeHint() const override {
        return m_pages->minimumSize();
    }
    bool hasHeightForWidth() const override {
        return m_pages->hasHeightForWidth();
    }
    int heightForWidth(int width) const override {
        return m_pages->heightForWidth(width);
    }
private:
    PageLayout* m_pages;
};
}
