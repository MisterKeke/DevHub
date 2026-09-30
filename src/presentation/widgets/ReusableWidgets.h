#pragma once

#include <QDialog>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QWidget>

class QLabel;
class QTextEdit;
class QVBoxLayout;

namespace devhub::presentation::widgets {

class AppLogoWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit AppLogoWidget(QWidget* parent = nullptr);
    void paintEvent(QPaintEvent* event) override;
};

class AccentButton final : public QPushButton
{
    Q_OBJECT
public:
    explicit AccentButton(const QString& text, QWidget* parent = nullptr);
};
class SecondaryButton final : public QPushButton
{
    Q_OBJECT
public:
    explicit SecondaryButton(const QString& text, QWidget* parent = nullptr);
};
class DestructiveButton final : public QPushButton
{
    Q_OBJECT
public:
    explicit DestructiveButton(const QString& text, QWidget* parent = nullptr);
};

class IconButton final : public QPushButton
{
    Q_OBJECT
public:
    IconButton(const QString& accessibleName, const QIcon& icon, QWidget* parent = nullptr);
};

class SearchField final : public QLineEdit
{
    Q_OBJECT
public:
    explicit SearchField(QWidget* parent = nullptr);
};

class PasswordField final : public QWidget
{
    Q_OBJECT
public:
    explicit PasswordField(QWidget* parent = nullptr);
    [[nodiscard]] QLineEdit* editor() const noexcept;

private:
    QLineEdit* m_editor = nullptr;
};

class FormField final : public QWidget
{
    Q_OBJECT
public:
    FormField(QString label, QWidget* editor, QString helpText = {}, bool required = false,
              QWidget* parent = nullptr);
    void setErrorText(const QString& text);

private:
    QLabel* m_error = nullptr;
};

class CardWidget final : public QFrame
{
    Q_OBJECT
public:
    explicit CardWidget(QWidget* parent = nullptr);
    [[nodiscard]] QVBoxLayout* contentLayout() const noexcept;
};

class StatusBadge final : public QFrame
{
    Q_OBJECT
public:
    StatusBadge(QString text, QString status = QStringLiteral("info"), QWidget* parent = nullptr);
};

class EmptyStateWidget final : public QWidget
{
    Q_OBJECT
public:
    EmptyStateWidget(QString title, QString message, QWidget* parent = nullptr);
};

class InlineErrorWidget final : public QFrame
{
    Q_OBJECT
public:
    InlineErrorWidget(QString message, QWidget* parent = nullptr);
};

class LoadingOverlay final : public QWidget
{
    Q_OBJECT
public:
    explicit LoadingOverlay(QWidget* parent = nullptr);
    void setMessage(const QString& message);
};

class ConfirmationDialog final : public QDialog
{
    Q_OBJECT
public:
    ConfirmationDialog(QString title, QString message, QWidget* parent = nullptr);
};

class TechnicalDetailsPanel final : public QFrame
{
    Q_OBJECT
public:
    explicit TechnicalDetailsPanel(QString details, QWidget* parent = nullptr);
};

class AuthenticationBackgroundWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit AuthenticationBackgroundWidget(QWidget* parent = nullptr);
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

private:
    void rebuildCache();
    QPixmap m_background;
};

} // namespace devhub::presentation::widgets
