#pragma once

#include <QDialog>
#include "../../domain/account/Username.h"

class QLabel;
class QLineEdit;
class QPushButton;

namespace devhub::application::authentication {
class AuthenticationService;
}
namespace devhub::domain::account {
}

namespace devhub::presentation::authentication {

class ChangePasswordDialog final : public QDialog
{
    Q_OBJECT
public:
    ChangePasswordDialog(const domain::account::NormalizedUsername& username,
                         application::authentication::AuthenticationService& service,
                         QWidget* parent = nullptr);

private:
    void submit();

    domain::account::NormalizedUsername m_username;
    application::authentication::AuthenticationService& m_service;
    QLineEdit* m_currentPassword = nullptr;
    QLineEdit* m_newPassword = nullptr;
    QLineEdit* m_confirmation = nullptr;
    QPushButton* m_saveButton = nullptr;
    QLabel* m_message = nullptr;
};

} // namespace devhub::presentation::authentication
