#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace devhub::presentation::authentication {

class ForgotPasswordInformationDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit ForgotPasswordInformationDialog(QWidget* parent = nullptr);
    [[nodiscard]] QString confirmationText() const;
    void setBusy(bool busy);
    void showMessage(const QString& message);

signals:
    void resetRequested();

private:
    QLineEdit* m_confirmation = nullptr;
    QPushButton* m_resetButton = nullptr;
    QLabel* m_message = nullptr;
};

} // namespace devhub::presentation::authentication
