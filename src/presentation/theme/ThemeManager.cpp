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
    m_application.setFont(ThemeTokens::font(TypographyToken::Body));
    if (systemHighContrastEnabled()) {
        m_application.setProperty("highContrast", true);
        return;
    }
    m_application.setProperty("highContrast", false);
    m_application.setPalette(ThemePalette::palette());
    m_application.setStyle(new ThemeProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))));

    const auto color = [](ColorToken token) { return ThemePalette::color(token).name(QColor::HexArgb); };
    QString sheet = QStringLiteral(
        "QWidget { color: %1; font-family: '%15'; }"
        "QMainWindow { background: %2; }"
        "QWidget#workspaceSidebar { background: %10; }"
        "QWidget#moduleNavigation { background: %2; }"
        "QWidget#contentRegion { background: %2; }"
        "QWidget#mainPageHost { background: %2; }"
        "QFrame#overviewCard { background: %3; border: 1px solid %4; border-radius: 0px; }"
        "QFrame#overviewCard QLabel#overviewCardTitle { font-size: 12pt; font-weight: 600; }"
        "QFrame#overviewCard QLabel#overviewSummary { color: %1; }"
        "QFrame#overviewCard QLabel#overviewDetail { color: %12; font-size: 9pt; padding: 2px 0; }"
        "QFrame#overviewCard QLabel#overviewDetailMuted { color: %12; font-size: 9pt; padding: 2px 0; }"
        "QFrame#overviewCard QLabel#overviewFreshness { color: %12; font-size: 9pt; }"
        "QFrame#overviewCard QPushButton#overviewAction { background: %8; border: 1px solid %4;"
        " border-radius: 0px; padding: 7px 12px; }"
        "QFrame#overviewCard QPushButton#overviewAction:hover { background: %7; border-color: %5; }"
        "QWidget#settingsSidebar { background: %10; border-right: 1px solid %4; }"
        "QListWidget#settingsNavigation { background: transparent; border: none; outline: none; }"
        "QListWidget#settingsNavigation::item { min-height: 22px; border-radius: 0px; padding: 8px 10px; margin: 2px 0; }"
        "QListWidget#settingsNavigation::item:hover { background: %7; }"
        "QListWidget#settingsNavigation::item:selected { color: %9; background: %5; border: 1px solid %5; }"
        "QFrame#settingsPanel { background: %3; border: 1px solid %4; border-radius: 0px; }"
        "QLabel#settingsPanelTitle { font-size: 11pt; font-weight: 600; }"
        "QLabel#settingsPageTitle { font-size: 18pt; font-weight: 600; }"
        "QLabel#settingsPageDescription { color: %12; }"
        "QLabel#settingsFieldDescription { color: %12; font-size: 9pt; }"
        "QLabel#settingsValueState { color: %12; font-size: 8pt; }"
        "QWidget#settingsFooter { background: %2; border-top: 1px solid %4; }"
        "QLabel#settingsDirtyState { color: %12; }"
        "QPushButton#saveSettingsButton { color: %9; background: %5; border-color: %5; font-weight: 600; }"
        "QPushButton#saveSettingsButton:hover { background: %6; border-color: %6; }"
        "QWidget#assistantMessageAssistant { background: %3; border: 1px solid %4; border-left: 3px solid %5; border-radius: 0px; }"
        "QWidget#assistantMessageUser { background: %8; border: 1px solid %5; border-radius: 0px; }"
        "QLabel#assistantMessageRoleAssistant { color: %5; font-weight: 600; }"
        "QLabel#assistantMessageRoleUser { color: %1; font-weight: 600; }"
        "QLabel#assistantMessageTimestamp, QLabel#assistantMessageStatus { color: %12; font-size: 9pt; }"
        "QWidget#assistantContextCard, QWidget#assistantConnectionCard { background: %3; border: 1px solid %4; border-radius: 0px; }"
        "QWidget#assistantComposerPanel { background: %8; border: 1px solid %5; border-radius: 0px; }"
        "QPlainTextEdit#assistantComposer { background: %3; border: 1px solid %4; border-radius: 0px; padding: 10px 12px; }"
        "QPlainTextEdit#assistantComposer:focus { border: 2px solid %6; }"
        "QLabel#assistantToolActivity { color: %5; }"
        "QLabel#assistantError { color: %13; }"
        "QPushButton#assistantSend { color: %9; background: %5; border-color: %5; font-weight: 600; min-width: 86px; }"
        "QPushButton#assistantSend:hover { background: %6; border-color: %6; }"
        "QPushButton#assistantStop { border-color: %13; }"
        "QWidget#assistantApprovalCard { background: %3; border: 1px solid %14; border-radius: 0px; }"
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { border-radius: 0px; padding: 8px 7px; margin: 1px 0; }"
        "QListWidget::item:hover { background: %7; }"
        "QListWidget::item:selected { background: %5; color: %9; }"
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox { background: %3; border: 1px solid %4;"
        " border-radius: 0px; padding: 7px 9px; selection-background-color: %5; }"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border: 2px solid %6; }"
        "QPushButton { background: %3; border: 1px solid %4; border-radius: 0px; padding: 7px 12px; }"
        "QPushButton:pressed, QToolButton:pressed { background: %11; }"
        "QPushButton:disabled, QToolButton:disabled { color: %12; }"
        "QPushButton#accentButton { color: %9; background: %5; border-color: %5; font-weight: 600; }"
        "QPushButton#accentButton:hover { background: %6; }"
        "QPushButton:hover, QToolButton:hover { background: %7; }"
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 0px; padding: 6px; }"
        "QMenu { background: %8; border: 1px solid %4; padding: 4px; }"
        "QMenu::item { padding: 7px 26px 7px 10px; border-radius: 0px; }"
        "QMenu::item:selected { background: %7; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 0px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QSplitter::handle { background: %4; width: 1px; }"
        "QToolTip { background: %8; border: 1px solid %4; padding: 4px; }"
        // Scope surfaces to actual controls so custom editors and model-provided
        // syntax, diff and severity colors remain in charge of their content.
        "QDialog, QMessageBox { background: %2; }"
        "QWidget#workspaceSidebar { border-right: 1px solid %5; }"
        "QWidget#moduleNavigation { border-right: 1px solid %4; }"
        "QFrame#overviewCard { border-top: 2px solid %5; }"
        "QFrame#overviewCard QLabel#overviewCardTitle { font-size: 11pt; }"
        "QPushButton:hover, QToolButton:hover { border-color: %6; }"
        "QPushButton:focus, QToolButton:focus { border: 2px solid %6; }"
        "QPushButton:pressed, QToolButton:pressed { background: %11; border-color: %5; }"
        "QPushButton:checked, QToolButton:checked { background: %11; border-color: %5; }"
        "QPushButton:disabled, QToolButton:disabled { background: %3; color: %16; border-color: %4; }"
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox { selection-color: %9; }"
        "QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QComboBox:disabled { color: %16; }"
        "QComboBox QAbstractItemView { background: %8; border: 1px solid %4; selection-background-color: %5; selection-color: %9; }"
        "QSpinBox, QDoubleSpinBox, QDateEdit, QTimeEdit, QDateTimeEdit { background: %3; border: 1px solid %4; padding: 6px; selection-background-color: %5; selection-color: %9; }"
        "QSpinBox:focus, QDoubleSpinBox:focus, QDateEdit:focus, QTimeEdit:focus, QDateTimeEdit:focus { border: 2px solid %6; }"
        "QTableView, QTreeView { background: %2; alternate-background-color: %3; border: 1px solid %4; gridline-color: %4; selection-background-color: %11; selection-color: %1; }"
        "QTableView::item, QTreeView::item { padding: 3px; }"
        "QTableView::item:selected, QTreeView::item:selected { background: %11; }"
        "QHeaderView::section { background: %8; color: %12; border: none; border-right: 1px solid %4; border-bottom: 1px solid %4; padding: 6px; }"
        "QTableCornerButton::section { background: %8; border: 1px solid %4; }"
        "QTabWidget::pane { background: %3; border: 1px solid %4; }"
        "QTabBar::tab { background: %3; color: %12; border: 1px solid %4; padding: 7px 12px; }"
        "QTabBar::tab:hover { background: %7; border-color: %6; }"
        "QTabBar::tab:selected { background: %5; color: %9; }"
        "QGroupBox { border: 1px solid %4; margin-top: 12px; padding-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; color: %12; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %4; background: %3; }"
        "QCheckBox::indicator:checked { background: %5; border: 3px solid %8; }"
        "QCheckBox::indicator:indeterminate { background: %6; border: 3px solid %8; }"
        "QCheckBox::indicator:hover, QCheckBox::indicator:focus { border-color: %6; }"
        "QCheckBox:focus, QRadioButton:focus { color: %6; }"
        "QCheckBox:disabled, QRadioButton:disabled { color: %16; }"
        "QCheckBox::indicator:disabled { background: %3; border-color: %4; }"
        "QCheckBox::indicator:checked:disabled, QCheckBox::indicator:indeterminate:disabled { background: %16; border: 3px solid %8; }"
        "QProgressBar { background: %2; border: 1px solid %4; text-align: center; }"
        "QProgressBar::chunk { background: %5; }"
        "QMenu::item:disabled { color: %16; }"
        "QMenu::separator { height: 1px; background: %4; margin: 4px; }"
        "QScrollBar:horizontal { background: %2; height: 10px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: %4; min-width: 24px; }"
        "QScrollBar::handle:hover { background: %6; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }");
    sheet.replace(QStringLiteral("%15"), ThemeTokens::fontFamily());
    sheet.replace(QStringLiteral("%16"), color(ColorToken::TextMuted));
    const std::array<QString, 14> colors{color(ColorToken::TextPrimary),
                                         color(ColorToken::WindowBackground),
                                         color(ColorToken::PanelSurface),
                                         color(ColorToken::BorderSubtle),
                                         color(ColorToken::Accent),
                                         color(ColorToken::FocusRing),
                                         color(ColorToken::HoverSurface),
                                         color(ColorToken::RaisedSurface),
                                         color(ColorToken::WindowBackground),
                                         color(ColorToken::SidebarSurface),
                                         color(ColorToken::AccentSoft),
                                         color(ColorToken::TextSecondary),
                                         color(ColorToken::Error),
                                         color(ColorToken::Warning)};
    for (int index = 14; index > 0; --index) {
        sheet.replace(QStringLiteral("%") + QString::number(index), colors.at(static_cast<size_t>(index - 1)));
    }
    m_application.setStyleSheet(sheet);
    m_application.setProperty("reducedMotion", m_reducedMotion);
}

QString ThemeManager::fontFamily() const { return ThemeTokens::fontFamily(); }
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
QColor ThemeManager::success() const { return ThemePalette::color(ColorToken::Success); }
QColor ThemeManager::warning() const { return ThemePalette::color(ColorToken::Warning); }
QColor ThemeManager::error() const { return ThemePalette::color(ColorToken::Error); }
QColor ThemeManager::info() const { return ThemePalette::color(ColorToken::Info); }

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
