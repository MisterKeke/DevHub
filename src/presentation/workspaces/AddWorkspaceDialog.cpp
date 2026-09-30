#include "AddWorkspaceDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace devhub::presentation::workspaces {
AddWorkspaceDialog::AddWorkspaceDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Add Workspace"));
    setMinimumWidth(480);
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr("Register a project folder. DevHub will keep its files in place."), this);
    description->setWordWrap(true); layout->addWidget(description);
    layout->addWidget(new QLabel(tr("Workspace name"), this));
    m_name = new QLineEdit(this); m_name->setObjectName(QStringLiteral("workspaceName")); layout->addWidget(m_name);
    layout->addWidget(new QLabel(tr("Folder"), this));
    auto* pathRow = new QHBoxLayout(); m_path = new QLineEdit(this); m_path->setObjectName(QStringLiteral("workspacePath"));
    auto* browse = new QPushButton(tr("Browse…"), this); pathRow->addWidget(m_path, 1); pathRow->addWidget(browse); layout->addLayout(pathRow);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add Workspace"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(buttons);
    connect(browse, &QPushButton::clicked, this, &AddWorkspaceDialog::chooseFolder);
    connect(m_path, &QLineEdit::textChanged, this, [this, buttons](const QString& path) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!path.trimmed().isEmpty() && !m_name->text().trimmed().isEmpty());
        if (m_name->text().isEmpty() && !path.isEmpty()) m_name->setText(QFileInfo(path).fileName());
    });
    connect(m_name, &QLineEdit::textChanged, this, [this, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_path->text().trimmed().isEmpty() && !m_name->text().trimmed().isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
QString AddWorkspaceDialog::displayName() const { return m_name->text().trimmed(); }
QString AddWorkspaceDialog::folderPath() const { return m_path->text().trimmed(); }
void AddWorkspaceDialog::chooseFolder()
{
    const QString start = m_path->text().isEmpty() ? QString() : m_path->text();
    const QString selected = QFileDialog::getExistingDirectory(this, tr("Select Workspace Folder"), start);
    if (!selected.isEmpty()) m_path->setText(selected);
}
}
