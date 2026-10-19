#include "CustomCursorController.h"

#include "../theme/ThemeManager.h"
#include "../theme/ThemePalette.h"

#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>

namespace devhub::presentation::cursor {
namespace {

constexpr int CursorDiameter = 46;
constexpr int OverlayDiameter = 72;
constexpr qint64 TrailLifetimeMs = 130;

QImage cropToVisiblePixels(const QString& resourcePath)
{
    const QImage source(resourcePath);
    if (source.isNull()) return {};

    int left = source.width();
    int top = source.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            if (qAlpha(source.pixel(x, y)) == 0) continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (right < left || bottom < top) return source;
    return source.copy(left, top, right - left + 1, bottom - top + 1);
}

} // namespace

class CursorOverlay final : public QWidget
{
public:
    explicit CursorOverlay(QWidget* parent = nullptr)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput
                              | Qt::WindowDoesNotAcceptFocus | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(OverlayDiameter, OverlayDiameter);
    }

    void setCursor(const QImage& image, const QPoint& position, const QColor& accent,
                   const QVector<CustomCursorController::TrailPoint>& trail)
    {
        m_image = image;
        m_position = position;
        m_accent = accent;
        m_trail = trail;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QPointF center(width() / 2.0, height() / 2.0);
        const auto trailCount = m_trail.size();
        for (int index = 0; index < trailCount; ++index) {
            const auto& sample = m_trail.at(index);
            const qreal freshness = std::clamp(1.0 - static_cast<qreal>(sample.ageMs)
                                                   / static_cast<qreal>(TrailLifetimeMs),
                                               0.0, 1.0);
            if (freshness <= 0.0) continue;
            const qreal distanceFade = static_cast<qreal>(index + 1)
                / static_cast<qreal>(trailCount + 1);
            const QPointF sampleCenter = center + QPointF(sample.position - m_position);
            const qreal radius = 1.5 + freshness * 2.5;
            QColor trailColor = m_accent;
            trailColor.setAlphaF(static_cast<float>(0.10 + freshness * (0.11 + 0.10 * distanceFade)));
            QRadialGradient glow(sampleCenter, radius * 2.8);
            glow.setColorAt(0.0, trailColor);
            trailColor.setAlpha(0);
            glow.setColorAt(1.0, trailColor);
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            painter.drawEllipse(sampleCenter, radius * 2.8, radius * 2.8);
        }

        if (!m_image.isNull()) {
            const QRectF target(center.x() - CursorDiameter / 2.0,
                                center.y() - CursorDiameter / 2.0,
                                CursorDiameter, CursorDiameter);
            painter.drawImage(target, m_image);
        }
    }

private:
    QImage m_image;
    QPoint m_position;
    QColor m_accent;
    QVector<CustomCursorController::TrailPoint> m_trail;
};

CustomCursorController::CustomCursorController(theme::ThemeManager& themeManager, QObject* parent)
    : QObject(parent), m_themeManager(themeManager), m_overlay(new CursorOverlay)
{
    m_blueCursorImage = cropToVisiblePixels(QStringLiteral(":/cursors/cursor-dark-blue.png"));
    m_roseCursorImage = cropToVisiblePixels(QStringLiteral(":/cursors/cursor-dark-rose.png"));
    updateTheme();
    m_trailTimer.setInterval(16);
    m_positionTimer.setInterval(16);
    connect(&m_trailTimer, &QTimer::timeout, this, &CustomCursorController::expireTrail);
    connect(&m_positionTimer, &QTimer::timeout, this, &CustomCursorController::refreshAtCurrentPosition);
    connect(&m_themeManager, &theme::ThemeManager::themeChanged,
            this, &CustomCursorController::updateTheme);
    qApp->installEventFilter(this);
    QTimer::singleShot(0, this, &CustomCursorController::refreshAtCurrentPosition);
}

CustomCursorController::~CustomCursorController()
{
    qApp->removeEventFilter(this);
    hideCursor();
    delete m_overlay;
}

bool CustomCursorController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_overlay) return false;

    switch (event->type()) {
    case QEvent::ApplicationDeactivate:
        hideCursor();
        break;
    case QEvent::ApplicationStateChange:
        if (QGuiApplication::applicationState() == Qt::ApplicationActive)
            QTimer::singleShot(0, this, &CustomCursorController::refreshAtCurrentPosition);
        else
            hideCursor();
        break;
    case QEvent::ApplicationActivate:
    case QEvent::WindowActivate:
        QTimer::singleShot(0, this, &CustomCursorController::refreshAtCurrentPosition);
        break;
    case QEvent::MouseMove:
        if (auto* mouseEvent = dynamic_cast<QMouseEvent*>(event))
            refreshAt(mouseEvent->globalPosition().toPoint());
        break;
    case QEvent::Enter:
    case QEvent::HoverMove:
        refreshAtCurrentPosition();
        break;
    case QEvent::Leave:
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
    case QEvent::Close:
        QTimer::singleShot(0, this, &CustomCursorController::refreshAtCurrentPosition);
        break;
    default:
        break;
    }
    return false;
}

QWindow* CustomCursorController::applicationWindowAt(const QPoint& globalPosition) const
{
#ifdef Q_OS_WIN
    const bool nativeHitTesting = QGuiApplication::platformName() == QStringLiteral("windows");
    HWND hoveredRoot = nullptr;
    if (nativeHitTesting) {
        // Use native screen coordinates: Qt's global coordinates are logical on
        // scaled displays. The input-transparent overlay is skipped by Windows.
        POINT position{};
        if (!GetCursorPos(&position)) return nullptr;
        hoveredRoot = GetAncestor(WindowFromPoint(position), GA_ROOT);
        if (hoveredRoot == nullptr) return nullptr;
    }
#endif

    // Geometry alone cannot detect the taskbar or another window covering us.
    const auto* overlayWindow = m_overlay != nullptr ? m_overlay->windowHandle() : nullptr;
    for (auto* window : QGuiApplication::allWindows()) {
        if (window == nullptr || window == overlayWindow || !window->isTopLevel()
            || !window->isVisible() || window->windowState() == Qt::WindowMinimized) continue;
#ifdef Q_OS_WIN
        if (nativeHitTesting && reinterpret_cast<HWND>(window->winId()) != hoveredRoot) continue;
#endif
        if (window->frameGeometry().contains(globalPosition)) return window;
    }
    return nullptr;
}

void CustomCursorController::refreshAt(const QPoint& globalPosition)
{
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        m_positionTimer.stop();
        hideCursor();
        return;
    }

    // Keep checking while active even outside the app: native title bars and
    // the taskbar do not necessarily deliver Qt mouse/enter/leave events.
    if (!m_positionTimer.isActive()) m_positionTimer.start();

    auto* targetWindow = applicationWindowAt(globalPosition);
    if (targetWindow == nullptr) {
        hideCursor();
        return;
    }

    if (!m_visible) {
        QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
        m_overrideCursorApplied = true;
        m_visible = true;
    }

    if (globalPosition != m_cursorPosition) {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        const int distance = (globalPosition - m_cursorPosition).manhattanLength();
        if (!m_themeManager.reducedMotion() && m_lastTrailSampleMs != 0
            && now - m_lastTrailSampleMs >= 8 && distance >= 2) {
            m_trail.prepend({m_cursorPosition, 0});
            while (m_trail.size() > 7) m_trail.removeLast();
            m_lastTrailSampleMs = now;
            if (!m_trailTimer.isActive()) m_trailTimer.start();
        } else if (m_lastTrailSampleMs == 0) {
            m_lastTrailSampleMs = now;
        }
    }

    m_cursorPosition = globalPosition;
    m_hoveredWindow = targetWindow;
    m_overlay->move(globalPosition - QPoint(OverlayDiameter / 2, OverlayDiameter / 2));
    if (!m_overlay->isVisible()) m_overlay->show();
    if (auto* overlayWindow = m_overlay->windowHandle()) {
        if (targetWindow->screen() != nullptr && overlayWindow->screen() != targetWindow->screen())
            overlayWindow->setScreen(targetWindow->screen());
        overlayWindow->setTransientParent(targetWindow);
    }
    m_overlay->raise();
    m_overlay->setCursor(m_cursorImage, globalPosition, theme::ThemePalette::color(theme::ColorToken::Accent), m_trail);
}

void CustomCursorController::refreshAtCurrentPosition()
{
    refreshAt(QCursor::pos());
}

void CustomCursorController::hideCursor()
{
    if (m_overlay != nullptr) m_overlay->hide();
    if (m_overrideCursorApplied) {
        QApplication::restoreOverrideCursor();
        m_overrideCursorApplied = false;
    }
    m_visible = false;
    m_hoveredWindow.clear();
    m_trail.clear();
    m_trailTimer.stop();
    m_lastTrailSampleMs = 0;
}

void CustomCursorController::updateTheme()
{
    m_cursorImage = theme::ThemePalette::variant() == theme::ThemeVariant::DarkBlue
        ? m_blueCursorImage : m_roseCursorImage;
    if (m_visible)
        m_overlay->setCursor(m_cursorImage, m_cursorPosition,
                             theme::ThemePalette::color(theme::ColorToken::Accent), m_trail);
}

void CustomCursorController::expireTrail()
{
    for (auto& sample : m_trail) sample.ageMs += m_trailTimer.interval();
    while (!m_trail.isEmpty() && m_trail.constLast().ageMs >= TrailLifetimeMs)
        m_trail.removeLast();
    if (m_visible)
        m_overlay->setCursor(m_cursorImage, m_cursorPosition,
                             theme::ThemePalette::color(theme::ColorToken::Accent), m_trail);
    if (m_trail.isEmpty()) m_trailTimer.stop();
}

} // namespace devhub::presentation::cursor
