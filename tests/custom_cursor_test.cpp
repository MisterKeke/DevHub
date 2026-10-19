#include "presentation/cursor/CustomCursorController.h"
#include "presentation/theme/ThemeManager.h"

#include <QApplication>
#include <QCursor>
#include <QScreen>
#include <QScopeGuard>
#include <QWidget>
#include <QWindow>
#include <QtTest/QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class CustomCursorTest final : public QObject
{
    Q_OBJECT

    static QWidget* overlay()
    {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (widget->windowFlags().testFlag(Qt::WindowTransparentForInput)) return widget;
        }
        return nullptr;
    }

private slots:
    void leavesAndReentersWithoutClicking()
    {
        const auto originalPosition = QCursor::pos();
        const auto restorePosition = qScopeGuard([&] { QCursor::setPos(originalPosition); });
        QWidget window;
        window.setWindowFlag(Qt::WindowStaysOnTopHint);
        const auto area = window.screen()->availableGeometry();
        window.setGeometry(QRect(area.center() - QPoint(200, 150), QSize(400, 300)));
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        // Embedded QML surfaces have parent-relative geometry. Their rectangles
        // must not make the desktop look like part of the application window.
        QWindow embedded(window.windowHandle());
        embedded.setGeometry(0, 0, area.width(), area.height());
        embedded.show();
        devhub::presentation::theme::ThemeManager theme(*qApp);
        devhub::presentation::cursor::CustomCursorController controller(theme);
        QCursor::setPos(window.geometry().center());
        QTRY_VERIFY(overlay() != nullptr && overlay()->isVisible());
        QVERIFY(QApplication::overrideCursor() != nullptr);

        // Remain active while hovering the desktop, without clicking elsewhere.
        QCursor::setPos(area.topLeft() + QPoint(10, 10));
        QTRY_VERIFY(!overlay()->isVisible());
        QVERIFY(QApplication::overrideCursor() == nullptr);
        QCOMPARE(QGuiApplication::applicationState(), Qt::ApplicationActive);

        // Polling must resume the overlay even without a Qt enter/mouse event.
        QCursor::setPos(window.geometry().center());
        QTRY_VERIFY(overlay()->isVisible());
        QVERIFY(QApplication::overrideCursor() != nullptr);
    }

    void coveringNativeWindowHidesCursorWithoutActivation()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Requires native Windows hit testing");
        const auto originalPosition = QCursor::pos();
        const auto restorePosition = qScopeGuard([&] { QCursor::setPos(originalPosition); });
        QWidget window;
        window.setWindowFlag(Qt::WindowStaysOnTopHint);
        window.resize(500, 400);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        devhub::presentation::theme::ThemeManager theme(*qApp);
        devhub::presentation::cursor::CustomCursorController controller(theme);
        QCursor::setPos(window.geometry().center());
        QTRY_VERIFY(overlay() != nullptr && overlay()->isVisible());

        POINT position{};
        QVERIFY(GetCursorPos(&position));
        const auto coveringWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            L"BUTTON", L"Cursor regression cover", WS_POPUP,
            position.x - 80, position.y - 80, 160, 160, nullptr, nullptr, nullptr, nullptr);
        QVERIFY(coveringWindow != nullptr);
        const auto destroyCover = qScopeGuard([&] { DestroyWindow(coveringWindow); });
        ShowWindow(coveringWindow, SW_SHOWNOACTIVATE);
        QTRY_VERIFY(!overlay()->isVisible());
        QVERIFY(QApplication::overrideCursor() == nullptr);
        QCOMPARE(QGuiApplication::applicationState(), Qt::ApplicationActive);
        ShowWindow(coveringWindow, SW_HIDE);
        QTRY_VERIFY(overlay()->isVisible());
#else
        QSKIP("Requires native Windows hit testing");
#endif
    }

    void maximizedWindowDoesNotDrawCursorOnTaskbar()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Requires the Windows taskbar");
        const auto taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        RECT bounds{};
        if (taskbar == nullptr || !IsWindowVisible(taskbar) || !GetWindowRect(taskbar, &bounds))
            QSKIP("No visible taskbar");
        const auto originalPosition = QCursor::pos();
        const auto restorePosition = qScopeGuard([&] { QCursor::setPos(originalPosition); });
        QWidget window;
        window.setWindowFlag(Qt::WindowStaysOnTopHint);
        window.showMaximized();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        devhub::presentation::theme::ThemeManager theme(*qApp);
        devhub::presentation::cursor::CustomCursorController controller(theme);
        QCursor::setPos(window.geometry().center());
        QTRY_VERIFY(overlay() != nullptr && overlay()->isVisible());
        QVERIFY(SetCursorPos((bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2));
        QTRY_VERIFY(!overlay()->isVisible());
        QVERIFY(QApplication::overrideCursor() == nullptr);
        QCOMPARE(QGuiApplication::applicationState(), Qt::ApplicationActive);
        QCursor::setPos(window.geometry().center());
        QTRY_VERIFY(overlay()->isVisible());
#else
        QSKIP("Requires the Windows taskbar");
#endif
    }
};

QTEST_MAIN(CustomCursorTest)
#include "custom_cursor_test.moc"
