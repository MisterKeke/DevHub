#pragma once

#include <QDialog>

class QLineEdit;

namespace devhub::presentation::workspaces {
class AddWorkspaceDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit AddWorkspaceDialog(QWidget* parent = nullptr);
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString folderPath() const;

private:
    void chooseFolder();
    QLineEdit* m_name = nullptr;
    QLineEdit* m_path = nullptr;
};
}
