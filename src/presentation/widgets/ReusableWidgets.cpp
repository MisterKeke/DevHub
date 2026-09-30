#include "ReusableWidgets.h"

#include "../icons/IconLoader.h"
#include "../theme/ThemePalette.h"
#include "../theme/ThemeTokens.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace devhub::presentation::widgets {
namespace {

using devhub::presentation::theme::ColorToken;
using devhub::presentation::theme::ThemePalette;
using devhub::presentation::theme::ThemeTokens;

QString colorName(ColorToken token)
{
    return ThemePalette::color(token).name(QColor::HexArgb);
}

} // namespace

AccentButton::AccentButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setObjectName(QStringLiteral("accentButton"));
    setMinimumHeight(ThemeTokens::size(devhub::presentation::theme::SizeToken::PrimaryAction));
    setStyleSheet(QStringLiteral("QPushButton#accentButton { background: %1; border: 1px solid %1; border-radius: 6px; font-weight: 600; padding: 8px 14px; } QPushButton#accentButton:hover { background: %2; } QPushButton#accentButton:pressed { background: %3; }")
                      .arg(colorName(ColorToken::Accent), colorName(ColorToken::AccentHover),
                           colorName(ColorToken::AccentPressed)));
}

SecondaryButton::SecondaryButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setObjectName(QStringLiteral("secondaryButton"));
    setMinimumHeight(ThemeTokens::size(devhub::presentation::theme::SizeToken::Control));
}

DestructiveButton::DestructiveButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setObjectName(QStringLiteral("destructiveButton"));
    setMinimumHeight(ThemeTokens::size(devhub::presentation::theme::SizeToken::Control));
    setStyleSheet(QStringLiteral("QPushButton#destructiveButton { color: %1; border: 1px solid %1; }")
                      .arg(colorName(ColorToken::Error)));
}

AppLogoWidget::AppLogoWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(42, 42);
    setAccessibleName(QStringLiteral("DevHub"));
}

void AppLogoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ThemePalette::color(ColorToken::ActiveSurface));
    painter.drawRoundedRect(rect(), ThemeTokens::radius(devhub::presentation::theme::RadiusToken::Card),
                            ThemeTokens::radius(devhub::presentation::theme::RadiusToken::Card));
    painter.drawPixmap(rect().adjusted(9, 9, -9, -9),
                       devhub::presentation::icons::IconLoader::icon(
                           devhub::presentation::icons::IconName::Logo,
                           ThemePalette::color(ColorToken::Accent), 24, devicePixelRatioF())
                           .pixmap(QSize(24, 24)));
}

IconButton::IconButton(const QString& accessibleName, const QIcon& icon, QWidget* parent)
    : QPushButton(parent)
{
    setIcon(icon);
    setIconSize(QSize(20, 20));
    setFixedSize(36, 36);
    setToolTip(accessibleName);
    setAccessibleName(accessibleName);
    setFocusPolicy(Qt::StrongFocus);
}

SearchField::SearchField(QWidget* parent)
    : QLineEdit(parent)
{
    setPlaceholderText(tr("Search"));
    setClearButtonEnabled(true);
    setAccessibleName(tr("Search workspace"));
    setMinimumHeight(ThemeTokens::size(devhub::presentation::theme::SizeToken::Control));
}

PasswordField::PasswordField(QWidget* parent)
    : QWidget(parent), m_editor(new QLineEdit(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Two));
    m_editor->setEchoMode(QLineEdit::Password);
    m_editor->setAccessibleName(tr("Password"));
    m_editor->setMinimumHeight(ThemeTokens::size(devhub::presentation::theme::SizeToken::Control));
    auto* toggle = new QToolButton(this);
    toggle->setText(tr("Show"));
    toggle->setAccessibleName(tr("Show password"));
    toggle->setCheckable(true);
    connect(toggle, &QToolButton::toggled, this, [this, toggle](bool visible) {
        m_editor->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        toggle->setText(visible ? tr("Hide") : tr("Show"));
        toggle->setAccessibleName(visible ? tr("Hide password") : tr("Show password"));
    });
    layout->addWidget(m_editor, 1);
    layout->addWidget(toggle);
}

QLineEdit* PasswordField::editor() const noexcept { return m_editor; }

FormField::FormField(QString labelText, QWidget* editor, QString helpText, bool required, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::One));
    auto* label = new QLabel(this);
    label->setText(required ? QStringLiteral("%1 *").arg(labelText) : labelText);
    label->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::CardTitle));
    layout->addWidget(label);
    if (editor != nullptr) {
        layout->addWidget(editor);
    }
    if (!helpText.isEmpty()) {
        auto* help = new QLabel(helpText, this);
        help->setWordWrap(true);
        help->setStyleSheet(QStringLiteral("color: %1;").arg(colorName(ColorToken::TextSecondary)));
        layout->addWidget(help);
    }
    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: %1;").arg(colorName(ColorToken::Error)));
    m_error->hide();
    layout->addWidget(m_error);
}

void FormField::setErrorText(const QString& text)
{
    m_error->setText(text);
    m_error->setVisible(!text.isEmpty());
}

CardWidget::CardWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("cardWidget"));
    setStyleSheet(QStringLiteral("QFrame#cardWidget { background: %1; border: 1px solid %2; border-radius: 8px; }")
                      .arg(colorName(ColorToken::PanelSurface), colorName(ColorToken::BorderSubtle)));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Four),
                               ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Four),
                               ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Four),
                               ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Four));
}

QVBoxLayout* CardWidget::contentLayout() const noexcept { return qobject_cast<QVBoxLayout*>(layout()); }

StatusBadge::StatusBadge(QString text, QString status, QWidget* parent)
    : QFrame(parent)
{
    const ColorToken token = status == QStringLiteral("success") ? ColorToken::Success
        : status == QStringLiteral("warning") ? ColorToken::Warning
        : status == QStringLiteral("error") ? ColorToken::Error : ColorToken::Info;
    setObjectName(QStringLiteral("statusBadge"));
    setStyleSheet(QStringLiteral("QFrame#statusBadge { background: %1; border: 1px solid %2; border-radius: 999px; }")
                      .arg(colorName(ColorToken::RaisedSurface), colorName(token)));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(9, 4, 9, 4);
    auto* label = new QLabel(std::move(text), this);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(colorName(token)));
    layout->addWidget(label);
}

EmptyStateWidget::EmptyStateWidget(QString title, QString message, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(ThemeTokens::spacing(devhub::presentation::theme::SpaceToken::Two));
    auto* heading = new QLabel(std::move(title), this);
    heading->setAlignment(Qt::AlignCenter);
    heading->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::PageTitle));
    auto* description = new QLabel(std::move(message), this);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    description->setMaximumWidth(480);
    description->setStyleSheet(QStringLiteral("color: %1;").arg(colorName(ColorToken::TextSecondary)));
    layout->addWidget(heading);
    layout->addWidget(description);
}

InlineErrorWidget::InlineErrorWidget(QString message, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("inlineError"));
    setStyleSheet(QStringLiteral("QFrame#inlineError { background: %1; border: 1px solid %2; border-radius: 6px; }")
                      .arg(colorName(ColorToken::PanelSurface), colorName(ColorToken::Error)));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    auto* label = new QLabel(std::move(message), this);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(colorName(ColorToken::TextPrimary)));
    layout->addWidget(label);
}

LoadingOverlay::LoadingOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("background: %1;").arg(colorName(ColorToken::WindowBackground)));
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    auto* label = new QLabel(tr("Loading…"), this);
    label->setObjectName(QStringLiteral("loadingMessage"));
    layout->addWidget(label);
}

void LoadingOverlay::setMessage(const QString& message)
{
    if (auto* label = findChild<QLabel*>(QStringLiteral("loadingMessage"))) {
        label->setText(message);
    }
}

ConfirmationDialog::ConfirmationDialog(QString title, QString message, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(std::move(title));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* label = new QLabel(std::move(message), this);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Confirm"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

TechnicalDetailsPanel::TechnicalDetailsPanel(QString details, QWidget* parent)
    : QFrame(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toggle = new QToolButton(this);
    toggle->setText(tr("Technical details"));
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    auto* text = new QTextEdit(this);
    text->setReadOnly(true);
    text->setPlainText(std::move(details));
    text->setFont(ThemeTokens::font(devhub::presentation::theme::TypographyToken::Monospace));
    text->setMaximumHeight(180);
    text->hide();
    connect(toggle, &QToolButton::toggled, this, [toggle, text](bool expanded) {
        toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        text->setVisible(expanded);
    });
    layout->addWidget(toggle);
    layout->addWidget(text);
}

AuthenticationBackgroundWidget::AuthenticationBackgroundWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void AuthenticationBackgroundWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_background.isNull()) {
        rebuildCache();
    }
    QPainter painter(this);
    painter.drawPixmap(QPoint(0, 0), m_background);
}

void AuthenticationBackgroundWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildCache();
}

bool AuthenticationBackgroundWidget::event(QEvent* event)
{
    const bool result = QWidget::event(event);
    if (event->type() == QEvent::ScreenChangeInternal || event->type() == QEvent::DevicePixelRatioChange) {
        rebuildCache();
        update();
    }
    return result;
}

void AuthenticationBackgroundWidget::rebuildCache()
{
    if (size().isEmpty()) {
        m_background = QPixmap();
        return;
    }
    const qreal dpr = devicePixelRatioF();
    m_background = QPixmap(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
    m_background.setDevicePixelRatio(dpr);
    m_background.fill(Qt::transparent);
    QPainter painter(&m_background);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor background = ThemePalette::color(ColorToken::WindowBackground);
    const QColor accent = ThemePalette::color(ColorToken::Accent);
    const auto translucentAccent = [accent](int alpha) {
        QColor result = accent;
        result.setAlpha(alpha);
        return result;
    };
    QLinearGradient base(0, 0, width(), height());
    base.setColorAt(0.0, background.lighter(135));
    base.setColorAt(0.48, background);
    base.setColorAt(1.0, background.darker(110));
    painter.fillRect(rect(), base);

    QRadialGradient glow(QPointF(width() * 0.51, height() * 0.48), qMax(width(), height()) * 0.58);
    glow.setColorAt(0.0, translucentAccent(20));
    glow.setColorAt(1.0, translucentAccent(0));
    painter.fillRect(rect(), glow);

    painter.setPen(Qt::NoPen);
    painter.setBrush(translucentAccent(16));
    QPolygonF upperLeft;
    upperLeft << QPointF(0, 0) << QPointF(width() * 0.20, 0)
              << QPointF(width() * 0.31, height() * 0.14)
              << QPointF(width() * 0.15, height() * 0.14);
    painter.drawPolygon(upperLeft);
    QPolygonF lowerRight;
    lowerRight << QPointF(width(), height()) << QPointF(width() * 0.84, height())
               << QPointF(width() * 0.74, height() * 0.83)
               << QPointF(width(), height() * 0.62);
    painter.setBrush(translucentAccent(18));
    painter.drawPolygon(lowerRight);

    QPen diagonal(translucentAccent(28));
    diagonal.setWidthF(1.0);
    painter.setPen(diagonal);
    QPainterPath leftPath;
    leftPath.moveTo(0, height() * 0.66);
    leftPath.lineTo(width() * 0.10, height() * 0.55);
    leftPath.lineTo(width() * 0.34, height() * 0.98);
    painter.drawPath(leftPath);
    QPainterPath rightPath;
    rightPath.moveTo(width(), height() * 0.34);
    rightPath.lineTo(width() * 0.90, height() * 0.23);
    rightPath.lineTo(width() * 0.66, height() * 0.99);
    painter.drawPath(rightPath);

    QPen orbit(translucentAccent(20));
    orbit.setWidthF(1.0);
    painter.setPen(orbit);
    painter.setBrush(Qt::NoBrush);
    const QPointF center(width() * 0.5, height() * 0.49);
    for (qreal radius : {qreal(118), qreal(168), qreal(218)}) {
        painter.drawEllipse(center, radius, radius);
    }
}

} // namespace devhub::presentation::widgets
