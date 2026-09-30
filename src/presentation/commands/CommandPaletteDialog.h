#pragma once

#include "../../application/commands/CommandRegistry.h"
#include "../../application/commands/ICommandSearchProvider.h"

#include <QDialog>
#include <QPointer>
#include <memory>
#include <vector>

class QLineEdit;
class QListWidget;
class QShowEvent;

namespace devhub::presentation::commands {

class CommandPaletteDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit CommandPaletteDialog(application::commands::CommandRegistry& registry, QWidget* parent = nullptr);
    void addProvider(application::commands::ICommandSearchProvider* provider);
    void openPalette();
protected:
    void showEvent(QShowEvent* event) override;
private:
    void beginSearch(QString query);
    void searchNextProvider(quint64 generation, QString query, int index);
    void executeCurrent();
    application::commands::CommandRegistry& m_registry;
    QLineEdit* m_query = nullptr;
    QListWidget* m_results = nullptr;
    QVector<application::commands::ICommandSearchProvider*> m_providers;
    std::vector<std::unique_ptr<application::commands::ICommandSearchProvider>> m_ownedProviders;
    QPointer<QWidget> m_previousFocus;
    quint64 m_generation = 0;
    bool m_acceptedSelection = false;
};

} // namespace devhub::presentation::commands
