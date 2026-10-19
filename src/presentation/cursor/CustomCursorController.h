#pragma once

#include <QObject>
#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QTimer>
#include <QVector>

class QWidget;
class QWindow;

namespace devhub::presentation::theme { class ThemeManager; }

namespace devhub::presentation::cursor {

class CursorOverlay;

class CustomCursorController final : public QObject
{
    Q_OBJECT
public:
    struct TrailPoint {
        QPoint position;
        qint64 ageMs = 0;
    };

    explicit CustomCursorController(theme::ThemeManager& themeManager, QObject* parent = nullptr);
    ~CustomCursorController() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refreshAt(const QPoint& globalPosition);
    void refreshAtCurrentPosition();
    void hideCursor();
    void updateTheme();
    void expireTrail();
    QWindow* applicationWindowAt(const QPoint& globalPosition) const;

    theme::ThemeManager& m_themeManager;
    CursorOverlay* m_overlay = nullptr;
    QTimer m_trailTimer;
    QTimer m_positionTimer;
    QVector<TrailPoint> m_trail;
    QImage m_cursorImage;
    QImage m_blueCursorImage;
    QImage m_roseCursorImage;
    QPoint m_cursorPosition;
    QPointer<QWindow> m_hoveredWindow;
    bool m_overrideCursorApplied = false;
    bool m_visible = false;
    qint64 m_lastTrailSampleMs = 0;
};

} // namespace devhub::presentation::cursor
