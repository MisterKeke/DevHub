#pragma once

#include <QObject>

class QApplication;

namespace devhub::presentation::theme {

class ThemeManager final : public QObject
{
    Q_OBJECT
public:
    explicit ThemeManager(QApplication& application);
    [[nodiscard]] bool reducedMotion() const noexcept;
    void setReducedMotion(bool enabled);

private:
    QApplication& m_application;
    bool m_reducedMotion = false;
};

} // namespace devhub::presentation::theme
