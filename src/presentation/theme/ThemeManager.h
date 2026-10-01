#pragma once

#include <QObject>
#include <QColor>

class QApplication;

namespace devhub::presentation::theme {

enum class ThemeVariant;

class ThemeManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY themeChanged)
    Q_PROPERTY(QColor background READ background NOTIFY themeChanged)
    Q_PROPERTY(QColor sidebarSurface READ sidebarSurface NOTIFY themeChanged)
    Q_PROPERTY(QColor panelSurface READ panelSurface NOTIFY themeChanged)
    Q_PROPERTY(QColor raisedSurface READ raisedSurface NOTIFY themeChanged)
    Q_PROPERTY(QColor hoverSurface READ hoverSurface NOTIFY themeChanged)
    Q_PROPERTY(QColor activeSurface READ activeSurface NOTIFY themeChanged)
    Q_PROPERTY(QColor borderSubtle READ borderSubtle NOTIFY themeChanged)
    Q_PROPERTY(QColor borderStrong READ borderStrong NOTIFY themeChanged)
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY themeChanged)
    Q_PROPERTY(QColor error READ error NOTIFY themeChanged)
public:
    explicit ThemeManager(QApplication& application);
    [[nodiscard]] bool reducedMotion() const noexcept;
    void setReducedMotion(bool enabled);
    [[nodiscard]] QColor accent() const;
    [[nodiscard]] QColor accentHover() const;
    [[nodiscard]] QColor background() const;
    [[nodiscard]] QColor sidebarSurface() const;
    [[nodiscard]] QColor panelSurface() const;
    [[nodiscard]] QColor raisedSurface() const;
    [[nodiscard]] QColor hoverSurface() const;
    [[nodiscard]] QColor activeSurface() const;
    [[nodiscard]] QColor borderSubtle() const;
    [[nodiscard]] QColor borderStrong() const;
    [[nodiscard]] QColor textPrimary() const;
    [[nodiscard]] QColor textSecondary() const;
    [[nodiscard]] QColor textMuted() const;
    [[nodiscard]] QColor error() const;
    void setVariant(ThemeVariant variant);

signals:
    void themeChanged();

private:
    void applyPalette();
    QApplication& m_application;
    bool m_reducedMotion = false;
};

} // namespace devhub::presentation::theme
