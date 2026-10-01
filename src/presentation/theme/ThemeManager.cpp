#include "ThemeManager.h"

#include "ThemePalette.h"
#include "ThemeProxyStyle.h"
#include "ThemeTokens.h"

#include <QApplication>
#include <QSettings>
#include <QStyleFactory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <array>

namespace devhub::presentation::theme {
namespace {

bool systemHighContrastEnabled()
{
#ifdef Q_OS_WIN
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
#else
    return false;
#endif
}

} // namespace

ThemeManager::ThemeManager(QApplication& application)
    : QObject(&application), m_application(application)
{
    m_reducedMotion = QSettings().value(QStringLiteral("appearance/reducedMotion"), false).toBool();
    m_application.setProperty("reducedMotion", m_reducedMotion);
    applyPalette();
}

void ThemeManager::applyPalette()
{
    if (systemHighContrastEnabled()) {
        m_application.setProperty("highContrast", true);
        return;
    }
    m_application.setProperty("highContrast", false);
    m_application.setPalette(ThemePalette::palette());
    m_application.setStyle(new ThemeProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    m_application.setFont(ThemeTokens::font(TypographyToken::Body));

    const auto color = [](ColorToken token) { return ThemePalette::color(token).name(QColor::HexArgb); };
    QString sheet = QStringLiteral(
        "QWidget { color: %1; font-family: 'Segoe UI'; }"
        "QMainWindow { background: %2; }"
        "QWidget#workspaceSidebar { background: %10; }"
        "QWidget#moduleNavigation { background: %2; }"
        "QWidget#contentRegion { background: %2; }"
        "QWidget#mainPageHost { background: %2; }"
        "QFrame#overviewCard { background: %3; border: 1px solid %4; border-radius: 12px; }"
        "QFrame#overviewCard QLabel#overviewCardTitle { font-size: 12pt; font-weight: 600; }"
        "QFrame#overviewCard QLabel#overviewSummary { color: %1; }"
        "QFrame#overviewCard QLabel#overviewDetail { color: %12; font-size: 9pt; padding: 2px 0; }"
        "QFrame#overviewCard QLabel#overviewDetailMuted { color: %12; font-size: 9pt; padding: 2px 0; }"
        "QFrame#overviewCard QLabel#overviewFreshness { color: %12; font-size: 9pt; }"
        "QFrame#overviewCard QPushButton#overviewAction { background: %8; border: 1px solid %4;"
        " border-radius: 6px; padding: 7px 12px; }"
        "QFrame#overviewCard QPushButton#overviewAction:hover { background: %7; border-color: %5; }"
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { border-radius: 5px; padding: 8px 7px; margin: 1px 0; }"
        "QListWidget::item:hover { background: %7; }"
        "QListWidget::item:selected { background: %11; color: %9; }"
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox { background: %3; border: 1px solid %4;"
        " border-radius: 6px; padding: 7px 9px; selection-background-color: %5; }"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border: 2px solid %6; }"
        "QPushButton { background: %3; border: 1px solid %4; border-radius: 6px; padding: 7px 12px; }"
        "QPushButton#accentButton { color: %9; }"
        "QPushButton:hover, QToolButton:hover { background: %7; }"
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 6px; }"
        "QMenu { background: %8; border: 1px solid %4; padding: 4px; }"
        "QMenu::item { padding: 7px 26px 7px 10px; border-radius: 4px; }"
        "QMenu::item:selected { background: %7; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QSplitter::handle { background: %4; width: 1px; }"
        "QToolTip { background: %8; border: 1px solid %4; padding: 4px; }");
    const std::array<QString, 12> colors{color(ColorToken::TextPrimary),
                                         color(ColorToken::WindowBackground),
                                         color(ColorToken::PanelSurface),
                                         color(ColorToken::BorderSubtle),
                                         color(ColorToken::Accent),
                                         color(ColorToken::FocusRing),
                                         color(ColorToken::HoverSurface),
                                         color(ColorToken::RaisedSurface),
                                         color(ColorToken::TextPrimary),
                                         color(ColorToken::SidebarSurface),
                                         color(ColorToken::AccentSoft),
                                         color(ColorToken::TextSecondary)};
    for (int index = 12; index > 0; --index) {
        sheet.replace(QStringLiteral("%") + QString::number(index), colors.at(static_cast<size_t>(index - 1)));
    }
    m_application.setStyleSheet(sheet);
    m_application.setProperty("reducedMotion", m_reducedMotion);
}

QColor ThemeManager::accent() const { return ThemePalette::color(ColorToken::Accent); }
QColor ThemeManager::accentHover() const { return ThemePalette::color(ColorToken::AccentHover); }
QColor ThemeManager::background() const { return ThemePalette::color(ColorToken::WindowBackground); }
QColor ThemeManager::sidebarSurface() const { return ThemePalette::color(ColorToken::SidebarSurface); }
QColor ThemeManager::panelSurface() const { return ThemePalette::color(ColorToken::PanelSurface); }
QColor ThemeManager::raisedSurface() const { return ThemePalette::color(ColorToken::RaisedSurface); }
QColor ThemeManager::hoverSurface() const { return ThemePalette::color(ColorToken::HoverSurface); }
QColor ThemeManager::activeSurface() const { return ThemePalette::color(ColorToken::ActiveSurface); }
QColor ThemeManager::borderSubtle() const { return ThemePalette::color(ColorToken::BorderSubtle); }
QColor ThemeManager::borderStrong() const { return ThemePalette::color(ColorToken::BorderStrong); }
QColor ThemeManager::textPrimary() const { return ThemePalette::color(ColorToken::TextPrimary); }
QColor ThemeManager::textSecondary() const { return ThemePalette::color(ColorToken::TextSecondary); }
QColor ThemeManager::textMuted() const { return ThemePalette::color(ColorToken::TextMuted); }
QColor ThemeManager::error() const { return ThemePalette::color(ColorToken::Error); }

void ThemeManager::setVariant(ThemeVariant variant)
{
    if (ThemePalette::variant() == variant) return;
    const QString value = variant == ThemeVariant::DarkBlue ? QStringLiteral("dark-blue")
                                                              : QStringLiteral("dark-rose");
    ThemePalette::setVariant(variant);
    QSettings().setValue(QStringLiteral("appearance/theme"), value);
    applyPalette();
    emit themeChanged();
}

bool ThemeManager::reducedMotion() const noexcept
{
    return m_reducedMotion;
}

void ThemeManager::setReducedMotion(bool enabled)
{
    if (m_reducedMotion == enabled) {
        return;
    }
    m_reducedMotion = enabled;
    QSettings().setValue(QStringLiteral("appearance/reducedMotion"), enabled);
    m_application.setProperty("reducedMotion", enabled);
}

} // namespace devhub::presentation::theme
